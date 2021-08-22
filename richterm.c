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

void resize(void);
void shutdown(void);
void send_interrupt(void);
void runcmd(void *);
void scroll(Point, Rich *);
void mouse(Mousectl *, Mouse, int *);
Object * getobj(Point xy);
long getsel(Point pt);

void mpaste(Rich *);
void msnarf(Rich *);
void mplumb(Rich *);
void msend(Rich *);

void newdraw(void);

Rich rich;
int hostpid = -1;
Channel *pidchan;
Mousectl *mctl;
Keyboardctl *kctl;
Devfsctl *dctl;
Fsctl *fsctl;
Array *fonts;
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg, *Itext;

char *mitems[] = {"paste", "snarf", "plumb", nil};
void (*mfunc[])(Rich *) = {mpaste, msnarf, mplumb, nil};

struct Menu mmenu = {
	.item = mitems,
	.gen = nil,
	.lasthit = 0,
};


Object *olast;

enum {
	MM_NONE,
	MM_SCROLLBAR,
	MM_TEXT,
	MM_SELECT,
};

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
	int rv[2], mmode;
	Mouse mv;
	Rune kv;
	Array *dv;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	} ARGEND

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

	Itext = display->black;

	fonts = arraycreate(sizeof(Font *), 2, nil);
	fp = arraygrow(fonts, 1);
	*fp = font;

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.objects = arraycreate(sizeof(Object *), 8, nil);
	rich.text = arraycreate(sizeof(char), 4096, nil);

	rich.page.scroll = ZP;

	rich.selmin = 0;
	rich.selmax = 0;

	qunlock(rich.l);

	olast = newobject(&rich, nil, 0);

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
			mouse(mctl, mv, &mmode);
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			resize();
			redraw(1);
			break;
		case KBD:
			if (kv == 0xf001) {
				draw(screen, rich.page.r, display->white, nil, ZP);
				newdraw();
				flushimage(display, 1);
				break;
			}
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
				/*TODO: should stop at last offset, not 0 */
				if (rich.text->count > 0) rich.text->count--;
				redraw(1);
				break;
			}
			if (rich.objects->count > 0) {
				int n;
				char *p;
				n = runelen(kv);

				qlock(rich.l);

				p = arraygrow(rich.text, n);
				runetochar(p, &kv);

				qunlock(rich.l);

				redraw(1);
			}
			if (kv == '\n') {
				qlock(rich.l);

				dv = arraycreate(sizeof(char),
				  rich.text->count - olast->offset, nil);
				arraygrow(dv, rich.text->count - olast->offset);
				memcpy(dv->p,
				  arrayget(rich.text, olast->offset),
				  rich.text->count - olast->offset);

				qunlock(rich.l);

				
				mkobjectftree(olast, fsctl->tree->root);
				olast = newobject(&rich, nil, 0);

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
mouse(Mousectl *mc, Mouse mv, int *mmode)
{
	static long selstart, selend;
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
			selstart = getsel(mv.xy);
			selend = selstart;
			rich.selmin = selstart;
			rich.selmax = selstart;
			redraw(0);
			*mmode = MM_SELECT;
		}
		if (mv.buttons == 2) {
			int f;
			f = menuhit(2, mc, &mmenu, nil);
			if (f >= 0) mfunc[f](&rich);
			*mmode = MM_NONE;
		}
		if (mv.buttons == 4) {
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
		selend = getsel(mv.xy);
		if (selstart < selend) {
			rich.selmin = selstart;
			rich.selmax = selend;
		} else {
			rich.selmin = selend;
			rich.selmax = selstart;
		}
		redraw(0);
	}
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
		fp = arraygrow(fonts, 1);
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
fauxalloc(Object *obj, Array *data, int type)
{
	Faux *aux;
	aux = mallocz(sizeof(Faux), 1);
	*aux = (Faux) {type, obj, data, arrayread, arraywrite};
	return aux;
}

Object *
newobject(Rich *rich, char *p, long n)
{
	Object *obj, **op, **old;
	qlock(rich->l);

	old = (rich->objects->count > 0) ?
	  arrayget(rich->objects, rich->objects->count - 1) : nil;

	op = arraygrow(rich->objects, 1);

	obj = mallocz(sizeof(Object), 1);

	*op = obj;

	if ((old != nil) && (*old != nil)) {
		(*old)->next = obj;
		obj->prev = *old;
	}

	obj->offset = rich->text->count;

	obj->font = font;
	obj->text = rich->text;

	obj->dlink = arraycreate(sizeof(char), 4096, nil);
	obj->dimage = arraycreate(sizeof(char), 4096, nil);

	if (p != nil) {
		char *pp;

		pp = arraygrow(rich->text, n);
		memcpy(pp, p, n);
	};

	qunlock(rich->l);
	return obj;
}

Object *
mkobjectftree(Object *obj, File *root)
{
	Faux *auxtext, *auxfont, *auxlink, *auximage;

	qlock(rich.l);

	obj->id = smprint("%ulld", ++rich.idcount);

	obj->dir = createfile(root, obj->id, "richterm", DMDIR|0555, nil);

	auxtext  = fauxalloc(obj, nil, FT_TEXT);
	auxfont  = fauxalloc(obj, nil, FT_FONT);
	auxlink  = fauxalloc(obj, obj->dlink, FT_LINK);
	auximage = fauxalloc(obj, obj->dimage, FT_IMAGE);

	auxtext->read = textread;
	auxtext->write = textwrite;

	auxfont->read = fontread;
	auxfont->write = fontwrite;

	obj->ftext  = createfile(obj->dir, "text",  "richterm", 0666, auxtext);
	obj->ffont  = createfile(obj->dir, "font",  "richterm", 0666, auxfont);
	obj->flink  = createfile(obj->dir, "link",  "richterm", 0666, auxlink);
	obj->fimage = createfile(obj->dir, "image", "richterm", 0666, auximage);

	qunlock(rich.l);
	return obj;
}


void
redraw(int)
{
	draw(screen, screen->r, Inormbg, nil, ZP);
	drawscrollbar();

	newdraw();

	flushimage(display, 1);
}

void
drawscrollbar(void)
{
	double D;
	Rectangle r;
	D =  (double)rich.page.max.y / (double)Dy(rich.page.r);
	if (D != 0) {
		r = rectaddpt(Rect(
		  0,  rich.page.scroll.y / D,
		  11, (rich.page.scroll.y + Dy(rich.page.r)) / D
		), rich.page.rs.min);
	} else {
		r = rich.page.rs;
		r.max.x--;
	};
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

void
mpaste(Rich *)
{
}

void
msnarf(Rich *)
{
}

void
mplumb(Rich *)
{
}

int
objectisvisible(Object *obj)
{
	return (obj->nextlinept.y >= rich.page.scroll.y) &&
		  (obj->startpt.y <= rich.page.scroll.y + Dy(rich.page.r));
}

void
_drawchar(Rune r, Point pt, Font *font, Image *fg, Image *bg)
{
	runestringnbg(screen,
	  subpt(addpt(pt, rich.page.r.min), rich.page.scroll),
	  fg, ZP, font, &r, 1, bg, ZP);
}

void
drawchar(Object *obj, long *n, Point *cur)
{
	int tabw, cw;
	Image *bg;
	Rune r;
	char *p;

	bg = ((*n >= rich.selmin) && (*n < rich.selmax)) ?
		Iselbg : Inormbg;

	p = arrayget(rich.text, *n);

	if (p == nil) return;

	*n += chartorune(&r, p);
	cw = runestringnwidth(obj->font, &r, 1);

	if (cur->x + cw >= Dx(rich.page.r)) {
		*cur = obj->nextlinept;
		obj->nextlinept.y += obj->font->height;
	}

	switch (*p) {
	case '\n':
		if (objectisvisible(obj))
			draw(screen,
			  rectaddpt(
				Rpt(*cur, Pt(Dx(rich.page.r), obj->nextlinept.y)),
			    subpt(rich.page.r.min, rich.page.scroll)),
			  bg, nil, ZP);

		cur->x = Dx(rich.page.r);
		break;
	case '\t':
		tabw = stringwidth(font, "0") * 4;
		if (objectisvisible(obj))
			draw(screen,
			  rectaddpt(
				Rpt(*cur, Pt(cur->x + tabw, obj->nextlinept.y)),
			    subpt(rich.page.r.min, rich.page.scroll)),
			  bg, nil, ZP);

		cur->x = (cur->x / tabw + 1) * tabw;
		break;
	default:
		if (objectisvisible(obj))
			_drawchar(r, *cur, obj->font, Itext, bg);
		cur->x += cw;
	}
}

void
drawobject(Object *obj, Point *cur)
{
	long i, n;
	if ((obj->offset > rich.text->count) || (obj->offset < 0)) {
		fprint(2, "drawobject: object out of bonds: %ld %ld\n",
		  obj->offset, rich.text->count);
		return;
	}

	if ((cur->x >= Dx(rich.page.r)) && (obj->prev != nil)) {
		*cur = obj->prev->nextlinept;
	}

	obj->startpt = *cur;
	obj->nextlinept = Pt(0, cur->y + obj->font->height);
	if ((obj->prev != nil) && (cur->x != 0)) {
		if (obj->nextlinept.y < obj->prev->nextlinept.y)
			obj->nextlinept.y = obj->prev->nextlinept.y;
	}
	if (obj->next == nil) n = rich.text->count;
	else n = obj->next->offset;
	for (i = obj->offset; i < n;) {
		drawchar(obj, &i, cur);
	}
	obj->endpt = *cur;
}

void
newdraw(void)
{
	Point cur;
	Object **op;
	long i;

	cur = ZP;
	qlock(rich.l);
	qlock(rich.text->l);
	for (i = 0; i < rich.objects->count; i++) {
		op = arrayget(rich.objects, i);
		drawobject(*op, &cur);
	}
	rich.page.max = cur;
	qunlock(rich.text->l);
	qunlock(rich.l);
}

Object *
getobj(Point xy)
{
	long i;
	Object **op, *obj;
	Point prevlinept;
	xy = subpt(xy, subpt(rich.page.r.min, rich.page.scroll));
	for (i = 0; i < rich.objects->count; i++) {
		op = arrayget(rich.objects, i);
		obj = *op;
		prevlinept = ( obj->prev == nil) ? ZP : obj->prev->nextlinept;

		if (((obj->startpt.y == obj->endpt.y) &&
		    (xy.y >= obj->startpt.y) &&
			(xy.y < obj->nextlinept.y) &&
		    (xy.x >= obj->startpt.x) &&
		    (xy.x < obj->endpt.x)) ||

		  ((xy.x >= obj->startpt.x) &&
		    (xy.y >= obj->startpt.y) &&
		    (xy.y < obj->endpt.y)) ||

		  ((obj->startpt.y < obj->endpt.y) &&
		    (xy.x < obj->endpt.x) &&
		    (xy.y >= obj->endpt.y) &&
		    (xy.y < obj->nextlinept.y)) ||

		  ((xy.y >= prevlinept.y) &&
		    (xy.y < obj->endpt.y))
		)
			return obj;
	}
	return nil;
}

long
getsel(Point xy)
{
	Object *obj;
	long n, i, li;
	Point cur, oldcur;

	obj = getobj(xy);

	xy = subpt(xy, subpt(rich.page.r.min, rich.page.scroll));

	if (obj == nil)
		return (xy.y > rich.page.max.y) ? rich.text->count : 0;

	n = (obj->next == nil) ? rich.text->count : obj->next->offset;
	li = obj->offset;

	cur = obj->startpt;
	obj->nextlinept = Pt(0, cur.y + obj->font->height);
	if ((obj->prev != nil) && (cur.x != 0)) {
		if (obj->nextlinept.y < obj->prev->nextlinept.y)
			obj->nextlinept.y = obj->prev->nextlinept.y;
	}

	for (i = obj->offset; i < n;) {
		oldcur = cur;
		li = i;
		drawchar(obj, &i, &cur);

		if (ptinrect(xy, Rpt(oldcur, Pt(cur.x, obj->nextlinept.y))) ||
		  (cur.y > xy.y))
			break;
	}
	return li;
}
