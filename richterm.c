#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include <9p.h>

#include "richterm.h"

Rich rich;
int hostpid = -1;
Channel *pidchan;
Devfsctl *dctl;
Fsctl *fsctl;
Fonts fonts;
Image *Iscrollbar, *Ilink;

void resize(void);
void shutdown(void);
void send_interrupt(void);
void runcmd(void *);
void scroll(Point, Rich *);

void
usage(void)
{
	fprint(2, "usage: %s [-D] [cmd]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Object *olast;
	char *ov;
	Mousectl *mctl;
	Keyboardctl *kctl;
	int rv[2], mmode;
	Mouse mv;
	Rune kv;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	} ARGEND

	if(rfork(RFENVG) < 0)
		sysfatal("rfork: %r");
	atexit(shutdown);

	pidchan = chancreate(sizeof(int), 0);
	proccreate(runcmd, argv, 16 * 1024);
	hostpid = recvul(pidchan);

	if (initdraw(0, 0, "richterm") < 0)
		sysfatal("%s: %r", argv0);

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.obj = nil;
	rich.count = 0;

	rich.page.scroll = ZP;
	rich.page.view = nil;

	qunlock(rich.l);

	mmode = 0;

	Iscrollbar = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x888888FF);
	Ilink = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DBlue);

	resize();
	redraw(1);

	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("%s: %r", argv0);
	if ((kctl = initkeyboard(nil)) == nil)
		sysfatal("%s: %r", argv0);

	enum {MOUSE, RESIZE, KBD, DEVFSWRITE, NONE};
	Alt alts[5] = {
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{kctl->c, &kv, CHANRCV},
		{dctl->wc, &ov, CHANRCV},
		{nil, nil, CHANEND},
	};
	for (;;) {
		switch(alt(alts)) {
		case MOUSE:
			if (mv.buttons == 0) mmode = 0;
			if (mv.buttons == 8) {
				scroll(subpt(rich.page.scroll, Pt(0, mv.xy.y - rich.page.r.min.y)), &rich);
				break;
			}
			if (mv.buttons == 16) {
				scroll(addpt(rich.page.scroll, Pt(0, mv.xy.y - rich.page.r.min.y)), &rich);
				break;
			}
			if (ptinrect(mv.xy, rich.page.rs) != 0) {
				if (mv.buttons == 1) {
					scroll(subpt(rich.page.scroll, Pt(0, mv.xy.y - rich.page.r.min.y)), &rich);
				} else if (mv.buttons == 4) {
					scroll(addpt(rich.page.scroll, Pt(0, mv.xy.y - rich.page.r.min.y)), &rich);
				} else if (mv.buttons == 2) {
					mmode = 1;
				}
			}
			if (mmode == 1) {
				int y;
				y = (mv.xy.y - rich.page.r.min.y) * ((double)rich.page.max.y / Dy(rich.page.r));
				scroll(Pt(rich.page.scroll.x, y), &rich);
			}
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			resize();
			redraw(1);
			break;
		case KBD:
			if (kv == 0x7f) shutdown();
			if (kv == 0xf00e) { /* d-pad up */
				scroll(Pt(0, rich.page.scroll.y - Dy(screen->r) / 8), &rich);
				break;
			}
			if (kv == 0xf800) { /* d-pad down */
				scroll(Pt(0, rich.page.scroll.y + Dy(screen->r) / 8), &rich);
				break;
			}
			if (kv == 0xf00f) { /* page up */
				scroll(Pt(0, rich.page.scroll.y - Dy(screen->r) / 4), &rich);
				break;
			}
			if (kv == 0xf013) { /* page down */
				scroll(Pt(0, rich.page.scroll.y + Dy(screen->r) / 4), &rich);
				break;
			}
			if (rich.obj != nil) {
				qlock(rich.l);

				/* TODO: make this utf8-aware */
				Faux *aux;
				olast = rich.obj + rich.count - 1;
				aux = olast->ftext->aux;
				aux->data->n++;
				aux->data->p = realloc(aux->data->p, aux->data->n + 1);
				aux->data->p[aux->data->n - 1] = kv;
				aux->data->p[aux->data->n] = 0;

				qunlock(rich.l);
			}
			redraw(1);
			nbsend(dctl->rc, &kv);
			break;
		case DEVFSWRITE:
			mkobjectftree(newobject(&rich), fsctl->tree->root, ov);
			redraw(1);
			break;
		case NONE:
			break;
		}
	}
}

void
drawpage(Image *dst, Page *p)
{
	int i;
	qlock(rich.l);
	for (i = 0; i < p->count; i++) {
		drawview(dst, p->view + i);
	}
	qunlock(rich.l);
}

void
drawview(Image *dst, View *v)
{
	Rectangle r;
	r = rectsubpt(v->r, v->page->scroll);
	draw(dst, r, display->white, nil, ZP);
	stringn(dst, r.min, v->color, ZP, v->font, v->dp, v->length);
}

void
generatepage(Rich *rich)
{
	#define BSIZE 4096

	Rectangle r;
	char *sp, *buf;
	Object *obj;
	int newline, tab, ymax;
	Point pt;
	Page *page;
	View *v;
	char *brkp;
	Faux *aux;

	qlock(rich->l);

	page = &rich->page;

	r = page->r;

	page->count = 0;
	page->scroll = rich->page.scroll;
	pt = r.min;
	ymax = 0;
	
	if (rich->obj == nil) {
		qunlock(rich->l);
		return;
	}

	obj = rich->obj;
	aux = obj->ftext->aux;
	sp = aux->data->p;
	while (obj < rich->obj + rich->count) {
		newline = 0;
		tab = 0;
		page->count++;
		page->view = realloc(page->view, sizeof(View) * (page->count));
		v = page->view + page->count - 1;

		v->obj = obj;
		v->color = display->black;
		if (((Faux *)obj->flink->aux)->data->n > 0) v->color = Ilink;
		v->page = &rich->page;

		buf = mallocz(((Faux *)obj->ffont->aux)->data->n + 1, 1);
		memcpy(buf, ((Faux *)obj->ffont->aux)->data->p, ((Faux *)obj->ffont->aux)->data->n);
		buf[((Faux *)obj->ffont->aux)->data->n] = '\0';
		v->font = getfont(&fonts, buf);
		free(buf);

		v->dp = sp;
		v->length = aux->data->n;

		if ((brkp = strpbrk(v->dp, "\n\t")) != 0) {
			v->length = brkp - v->dp;
			sp = v->dp + v->length + 1;
			switch (*brkp) {
			case '\n':
				newline = 1;
				break;
			case '\t':
				tab = 1;
				break;
			}
		}
		while (stringnwidth(v->font, v->dp, v->length) > (r.max.x - pt.x)) {
			newline = 1;
			v->length--;
			sp = v->dp + v->length;
		}

		v->r = Rpt(pt, Pt(pt.x + stringnwidth(v->font, v->dp, v->length),
			pt.y + v->font->height));

		ymax = (ymax > v->r.max.y) ? ymax : v->r.max.y;
		pt.x = v->r.max.x;
		if (tab != 0) {
			int nx, tl;
			nx = r.min.x;
			tl = stringwidth(font, "0") * 4;
			while (nx <= pt.x){
				nx += tl;
				if (nx > r.max.x) {
					newline = 1;
					break;
				}
			}
			pt.x = nx;
		}
		if (newline != 0) {
			pt.x = r.min.x;
			pt.y = ymax;
		}

		if (v->length >= aux->data->n - 1) {
			obj++;
			if (obj < rich->obj + rich->count) {
				aux = obj->ftext->aux;
				sp = aux->data->p;
			}
		}
	}

	rich->page.max.y = ymax - r.min.y;
	rich->page.max.x = 0;

	qunlock(rich->l);
}

Font *
getfont(struct Fonts *fonts, char *name)
{
	int i;
	Font *newfont;
	for (i = 0; i < fonts->count; i++){
		if (strcmp(fonts->data[i]->name, name) == 0) return fonts->data[i];
	}
	if ((newfont = openfont(display, name)) == nil) {
		fprint(2, "%r\n");
		newfont = font;
	} else {
		addfont(fonts, newfont);
	}
	return newfont;
}

void
addfont(struct Fonts *fonts, Font *font)
{
	if (fonts->data == nil) {
		fonts->data = mallocz(16 * sizeof(Font*), 1);
		fonts->size = 16;
	}
	if (fonts->count >= fonts->size) {
		fonts->size = fonts->size * 2;
		fonts->data = realloc(fonts->data, fonts->size * sizeof(Font*));
	}
	fonts->data[fonts->count] = font;
	fonts->count++;
}

void
scroll(Point p, Rich *r)
{
	if (p.x < 0) p.x = 0;
	if (p.x > r->page.max.x) p.x = r->page.max.x;
	if (p.y < 0) p.y = 0;
	if (p.y > r->page.max.y) p.y = r->page.max.y;

	r->page.scroll = p;

	redraw(0);
}

Faux *
fauxalloc(char *str)
{
	Faux *aux;
	aux = mallocz(sizeof(Faux), 1);
	aux->data = mallocz(sizeof(Data), 1);
	aux->data->p = str;
	aux->data->n = strlen(str);
	return aux;
}

Object *
newobject(Rich *rich)
{
	Object *obj;
	qlock(rich->l);
	rich->count++;
	rich->obj = realloc(rich->obj, rich->count * sizeof(Object));
	obj = &(rich->obj[rich->count - 1]);
	obj->id = smprint("%ld", rich->count);
	qunlock(rich->l);
	return obj;
}

Object *
mkobjectftree(Object *obj, File *root, char *text)
{
	Faux *auxtext, *auxfont, *auxlink, *auximage;

	qlock(rich.l);

	obj->dir = createfile(root, obj->id, "richterm", DMDIR|0555, nil);

	auxtext  = fauxalloc(text);
	auxfont  = fauxalloc(strdup(font->name));
	auxlink  = fauxalloc(strdup(""));
	auximage = fauxalloc(strdup(""));

	obj->ftext  = createfile(obj->dir, "text",  "richterm", 0666, auxtext);
	obj->ffont  = createfile(obj->dir, "font",  "richterm", 0666, auxfont);
	obj->flink  = createfile(obj->dir, "link",  "richterm", 0666, auxlink);
	obj->fimage = createfile(obj->dir, "image", "richterm", 0666, auximage);
	qunlock(rich.l);
	return obj;
}

void
redraw(int regen)
{
	if (regen != 0) generatepage(&rich);
	draw(screen, screen->r, display->white, nil, ZP);
	drawpage(screen, &rich.page);
	drawscrollbar();
	flushimage(display, 1);
}

void
drawscrollbar(void)
{
	double D;
	Rectangle r;

	D =  (double)rich.page.max.y / (double)Dy(rich.page.r);
	if (D == 0) return;

	r = rectaddpt(Rect(
	  0,  rich.page.scroll.y / D,
	  11, (rich.page.scroll.y + Dy(rich.page.r)) / D
	), rich.page.rs.min);

	draw(screen, rich.page.rs, Iscrollbar, nil, ZP);
	draw(screen, r, display->white, nil, ZP);
}

void
runcmd(void *args)
{
	char **argv = args;
	char *cmd;
	
	rfork(RFNAMEG);

	if ((dctl = initdevfs()) == nil)
		sysfatal("initdevfs failed: %r");
	if ((fsctl = initfs()) == nil)
		sysfatal("initfs failed: %r");

	rfork(RFFDG);
	close(0);
	open("/dev/cons", OREAD);
	close(1);
	open("/dev/cons", OWRITE);
	dup(1, 2);
	
	cmd = nil;
	while (*argv != nil) {
		if (cmd == nil) cmd = strdup(*argv);
		else cmd = smprint("%s %q", cmd, *argv);
		argv++;
	}

	procexecl(pidchan, "/bin/rc", "rcX", cmd == nil ? nil : "-c", cmd, nil);
	sysfatal("%r");
}

void
shutdown(void)
{
	send_interrupt();
	threadexitsall(nil);
}

void
send_interrupt(void)
{
	if(hostpid > 0)
		postnote(PNGROUP, hostpid, "interrupt");
}

void
resize(void)
{
	rich.page.rs = Rpt(
	  addpt(Pt(1,1), screen->r.min),
	  Pt(screen->r.min.x + 13, screen->r.max.y - 1)
	);
	rich.page.r = Rpt(
	  addpt(screen->r.min, Pt(17, 1)),
	  subpt(screen->r.max, Pt(1,1))
	);
}
