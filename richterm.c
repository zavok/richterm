#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include <9p.h>

#include "array.h"
#include "richterm.h"

Rich rich;
int hostpid = -1;
Channel *pidchan;
Devfsctl *dctl;
Fsctl *fsctl;
Array *fonts;
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg;
Object *olast;

enum {
	MM_NONE,
	MM_SCROLLBAR,
	MM_TEXT,
	MM_SELECT,
};

void resize(void);
void shutdown(void);
void send_interrupt(void);
void runcmd(void *);
void scroll(Point, Rich *);
void mouse(Mouse, int *);
usize getsel(Point p);
View * getview(Point p);

void
usage(void)
{
	fprint(2, "usage: %s [-D] [cmd]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Font **fp;
	Mousectl *mctl;
	Keyboardctl *kctl;
	int rv[2], mmode;
	Mouse mv;
	Rune kv;
	Data dv;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	} ARGEND

	dv = (Data) {nil, 0};
	mmode = MM_NONE;

	if (initdraw(0, 0, "richterm") < 0)
		sysfatal("%s: %r", argv0);
	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("%s: %r", argv0);
	if ((kctl = initkeyboard(nil)) == nil)
		sysfatal("%s: %r", argv0);

	Iscrollbar = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, 0x999999FF);
	Inormbg = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, 0xDDDDDDFF);
	Iselbg = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, 0xBBBBBBFF);
	Ilink = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, DBlue);

	fonts = arraycreate(sizeof(Font *), 2, nil);
	fp = arrayadd(fonts);
	*fp = font;

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.objects = arraycreate(sizeof(Object *), 8, nil);
	rich.page.views = arraycreate(sizeof(View), 8, nil);

	rich.page.scroll = ZP;

	rich.page.selstart = 0;
	rich.page.selend = 0;

	qunlock(rich.l);

	olast = newobject(&rich, nil);

	resize();
	redraw(1);

	if(rfork(RFENVG) < 0)
		sysfatal("rfork: %r");
	atexit(shutdown);

	pidchan = chancreate(sizeof(int), 0);
	proccreate(runcmd, argv, 16 * 1024);
	hostpid = recvul(pidchan);

	enum {MOUSE, RESIZE, KBD, DEVFSWRITE, NONE};
	Alt alts[5] = {
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{kctl->c, &kv, CHANRCV},
		{nil, nil, CHANEND},
	};
	for (;;) {
		switch(alt(alts)) {
		case MOUSE:
			mouse(mv, &mmode);
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			resize();
			redraw(1);
			break;
		case KBD:
			if (kv == 0x7f) shutdown(); /* delete */
			if (kv == 0xf00e) { /* d-pad up */
				scroll(
				  Pt(0, rich.page.scroll.y - Dy(screen->r) / 8),
				  &rich);
				break;
			}
			if (kv == 0xf800) { /* d-pad down */
				scroll(
				  Pt(0, rich.page.scroll.y + Dy(screen->r) / 8),
				  &rich);
				break;
			}
			if (kv == 0xf00f) { /* page up */
				scroll(
				  Pt(0, rich.page.scroll.y - Dy(screen->r) / 4),
				  &rich);
				break;
			}
			if (kv == 0xf013) { /* page down */
				scroll(
				  Pt(0, rich.page.scroll.y + Dy(screen->r) / 4),
				  &rich);
				break;
			}
			if (kv == 0x08) { /* backspace */
				if (olast->dtext->n > 0) olast->dtext->n--;
				redraw(1);
				break;
			}
			if (rich.objects->count > 0) {
				int n;
				n = runelen(kv);

				qlock(rich.l);

				olast->dtext->n+=n;
				olast->dtext->p = realloc(
				  olast->dtext->p, olast->dtext->n + 1);

				runetochar(olast->dtext->p + olast->dtext->n - n, &kv);
				olast->dtext->p[olast->dtext->n] = '\0';

				qunlock(rich.l);

				redraw(1);
			}
			if (kv == '\n') {
				Object *obj;
				obj = mkobjectftree(newobject(&rich, nil),
				  fsctl->tree->root);
				qlock(rich.l);

				obj->dtext->n = olast->dtext->n;
				obj->dtext->p = realloc(obj->dtext->p, obj->dtext->n);
				memcpy(obj->dtext->p, olast->dtext->p, olast->dtext->n);

				olast->dtext->n = 0;

				dv.p = mallocz(obj->dtext->n, 1);
				dv.n = obj->dtext->n;
				memcpy(dv.p, obj->dtext->p, dv.n);

				qunlock(rich.l);

				nbsend(dctl->rc, &dv);

				redraw(1);
				break;
			}
			break;
		case NONE:
			break;
		}
	}
}

void
mouse(Mouse mv, int *mmode)
{
	View *v;

	if (mv.buttons == 0) {
		*mmode = MM_NONE;
		return;
	}
	if (mv.buttons == 8) {
		scroll(
		  subpt(rich.page.scroll,
		  Pt(0, mv.xy.y - rich.page.r.min.y)),
		  &rich);
		return;
	}
	if (mv.buttons == 16) {
		scroll(
		  addpt(rich.page.scroll,
		  Pt(0, mv.xy.y - rich.page.r.min.y)),
		  &rich);
		return;
	}

	if (*mmode == MM_NONE) {
		if (ptinrect(mv.xy, rich.page.rs) != 0)
			*mmode = MM_SCROLLBAR;
		if (ptinrect(mv.xy, rich.page.r) != 0)
			*mmode = MM_TEXT;
	}

	switch (*mmode) {
	case MM_SCROLLBAR:
		if (mv.buttons == 1) {
			scroll(
			  subpt(rich.page.scroll,
			  Pt(0, mv.xy.y - rich.page.r.min.y)),
			  &rich);
		} else if (mv.buttons == 2) {
			scroll(
			  Pt(rich.page.scroll.x,
			    (mv.xy.y - rich.page.r.min.y) *
		  	    ((double)rich.page.max.y / Dy(rich.page.r))),
		 	  &rich);
		} else if (mv.buttons == 4) {
			scroll(
			  addpt(rich.page.scroll,
			  Pt(0, mv.xy.y - rich.page.r.min.y)),
			  &rich);
		}
		break;
	case MM_TEXT:
		if (mv.buttons == 1) {
			rich.page.selstart = getsel(mv.xy);
			rich.page.selend = rich.page.selstart;
			redraw(1);
			*mmode = MM_SELECT;
		}
		if (mv.buttons == 4) {
			Object *obj;
			Data *dlink;
			v = getview(mv.xy);
			obj = nil;
			dlink = nil;
			if (v != nil) obj = v->obj;
			if (obj != nil) dlink = obj->dlink;
			if ((dlink != nil) && (dlink->n > 0)) {
				Data dv;
				dv.n = dlink->n;
				dv.p = malloc(dlink->n);
				memcpy(dv.p, dlink->p, dv.n);
				nbsend(dctl->rc, &dv);
			}
			break;
		}
	case MM_SELECT:
		if (mv.buttons == (1|2)) {
			/* cut */
			break;
		}
		if (mv.buttons == (1|4)) {
			/* paste */
			break;
		}
		rich.page.selend = getsel(mv.xy);
		redraw(1);
	}
}

usize
getsel(Point p)
{
	View *v, *vp;
	long i;
	usize cc;
	cc = 0;
	v = getview(p);
	if (v == nil) return 0;
	for (i = 0; i < rich.page.views->count ; i++) {
		vp = arrayget(rich.page.views, i);
		if (v == vp) break;
		cc += vp->length;
	}
	for (i = 0; i < v->length; i++) {
		if (stringnwidth(v->obj->font, v->dp, i) >=
		  p.x - v->r.min.x)
			break;
	}
	return cc + i - 1;
}

View *
getview(Point p)
{
	int i;
	View *vp;

	if (p.x < rich.page.r.min.x) p.x = rich.page.r.min.x;
	if (p.x > rich.page.r.max.x) p.x = rich.page.r.max.x;

	for (i = 0; i < rich.page.views->count; i++) {
		vp = arrayget(rich.page.views, i);
		if (ptinrect(p, vp->r) != 0)
			return vp;
	}
	return nil;
}

void
drawpage(Image *dst, Page *p)
{
	View *vp;
	int i;
	qlock(rich.l);
	for (i = 0; i < p->views->count; i++) {
		vp = arrayget(p->views, i);
		drawview(dst, vp);
	}
	qunlock(rich.l);
}

void
drawview(Image *dst, View *v)
{
	Image *bg;
	Rectangle r;
	r = rectsubpt(v->r, v->page->scroll);
	if (v->image != nil) {
		draw(dst, r, v->image, nil, ZP);
	} else {
		bg = (v->selected != 0) ? Iselbg : Inormbg;
		draw(dst, r, bg, nil, ZP);
		stringn(dst, r.min, v->color, ZP,
		  v->obj->font, v->dp, v->length);
	}
}

void
generatepage(Rich *rich)
{
	Rectangle r;
	char *sp;
	usize cc;
	Object *obj, **op;
	int sel, ymax, i;
	usize selmin, selmax;
	Point pt;
	Page *page;

	enum {
		SEL_BEFORE,
		SEL_IN,
		SEL_AFTER
	};

	sel = SEL_BEFORE;
	cc = 0;

	if (rich->objects->count == 0) return;

	qlock(rich->l);
	
	page = &rich->page;

	page->views->count = 0;

	r = page->r;

	if (page->selstart < page->selend) {
		selmin = page->selstart;
		selmax = page->selend;
	} else {
		selmin = page->selend;
		selmax = page->selstart;
	}

	pt = r.min;
	ymax = 0;

	op = arrayget(rich->objects, 0);
	obj = *op;
	sp = obj->dtext->p;
	i = 0;
	while (i < rich->objects->count) {
		int newline, tab;
		View *v;
		char *brkp;

		newline = 0;
		tab = 0;

		v = arrayadd(rich->page.views);

		v->obj = obj;
		v->color = display->black;
		if (obj->dlink->n > 0) v->color = Ilink;
		v->page = &rich->page;

		v->image = nil;
		v->selected = (sel == SEL_IN);

		v->dp = sp;

		/* TODO: next line is probably incorrect */
		v->length = obj->dtext->n;

		/* this how it should look in theory, but it doesn't work:
		 * v->length = obj->dtext->p + obj->dtext->n - sp + 1;
		 */

		if ((sel==SEL_BEFORE) &&
		  (v->length > selmin - cc)) {
			v->length = selmin - cc;
			sp = v->dp + v->length;
			sel = SEL_IN;
		}
		if ((sel==SEL_IN) &&
		  (v->length > selmax - cc)) {
			v->length = selmax - cc;
			sp = v->dp + v->length;
			sel = SEL_AFTER;
		}

		/* TODO: v->dp is not guaranteed to be null-terminated
		 * so, rework following section without strpbrk
		 */
		if (
		  ((brkp = strpbrk(v->dp, "\n\t")) != 0) &&
		  (brkp <= v->dp + v->length)) {
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
		while (stringnwidth(v->obj->font, v->dp, v->length) > (r.max.x - pt.x)) {
			newline = 1;
			v->length--;
			sp = v->dp + v->length;
		}

		v->r = Rpt(
		  pt,
		  Pt(pt.x + stringnwidth(v->obj->font, v->dp, v->length),
		    pt.y + v->obj->font->height));

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
			v->r.max.x = nx;
			pt.x = nx;
		}
		if (newline != 0) {
			v->r.max.x = r.max.x;
			pt.x = r.min.x;
			pt.y = ymax;
		}

		if (v->length >= obj->dtext->n - 1) {
			i++;
			op = arrayget(rich->objects, i);
			obj = *op;
			if (i < rich->objects->count) {
				sp = obj->dtext->p;
			}
		}

		cc += v->length;
	}

	page->max.y = ymax - r.min.y;
	page->max.x = r.max.x - r.min.x;

	qunlock(rich->l);
}

Font *
getfont(Array *fonts, char *name)
{
	int i;
	Font *newfont, **fp;
	for (i = 0; i < fonts->count; i++){
		fp = arrayget(fonts, i);
		if (strcmp((*fp)->name, name) == 0) return *fp;
	}
	if ((newfont = openfont(display, name)) == nil) {
		fprint(2, "%r\n");
		newfont = font;
	} else {
		fp = arrayadd(fonts);
		*fp = newfont;
	}
	return newfont;
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
fauxalloc(Object *obj, Data *data, int type)
{
	Faux *aux;
	aux = mallocz(sizeof(Faux), 1);
	aux->obj = obj;
	aux->data = data;
	aux->type = type;
	return aux;
}

Object *
newobject(Rich *rich, char *text)
{
	Object *obj, **op;
	qlock(rich->l);

	op = arrayadd(rich->objects);

	if (rich->objects->count > 1) {
		Object **o1;
		o1 = arrayget(rich->objects, rich->objects->count - 2);
		*op = *o1;
		op = o1;
	}

	obj = mallocz(sizeof(Object), 1);

	*op = obj;

	obj->dtext = mallocz(sizeof(Data), 1);
	obj->dfont = mallocz(sizeof(Data), 1);
	obj->dlink = mallocz(sizeof(Data), 1);
	obj->dimage = mallocz(sizeof(Data), 1);

	if (text != nil) {
		obj->dtext->p = text;
		obj->dtext->n = strlen(text);
	} else obj->dtext->p = strdup("");

	obj->dfont->p = strdup(font->name);
	obj->dfont->n = strlen(font->name);

	obj->dlink->p = strdup("");
	obj->dimage->p = strdup("");

	obj->id = smprint("%lld", rich->idcount);

	obj->font = font;

	rich->idcount++;

	qunlock(rich->l);
	return obj;
}

Object *
mkobjectftree(Object *obj, File *root)
{
	Faux *auxtext, *auxfont, *auxlink, *auximage;

	qlock(rich.l);

	obj->dir = createfile(root, obj->id, "richterm", DMDIR|0555, nil);

	auxtext  = fauxalloc(obj, obj->dtext, FT_TEXT);
	auxfont  = fauxalloc(obj, obj->dfont, FT_FONT);
	auxlink  = fauxalloc(obj, obj->dlink, FT_LINK);
	auximage = fauxalloc(obj, obj->dimage, FT_IMAGE);

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
	draw(screen, screen->r, Inormbg, nil, ZP);
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
	draw(screen, r, Inormbg, nil, ZP);
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

void
objectfree(void *v)
{
	Object *op;
	op = v;

	removefile(op->ftext);
	removefile(op->ffont);
	removefile(op->flink);
	removefile(op->fimage);
	removefile(op->dir);

	/* TODO: why is this not working? */
	free(op->id);

//	free(op->dtext->p);
//	free(op->dfont->p);
//	free(op->dlink->p);
//	free(op->dimage->p);

	free(op);
}