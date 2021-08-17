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
usize getsel(Point p);
View * getview(Point p);

void mpaste(Rich *);
void msnarf(Rich *);
void mplumb(Rich *);
void msend(Rich *);

Rich rich;
int hostpid = -1;
Channel *pidchan;
Mousectl *mctl;
Keyboardctl *kctl;
Devfsctl *dctl;
Fsctl *fsctl;
Array *fonts;
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg;

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

	fonts = arraycreate(sizeof(Font *), 2, nil);
	fp = arraygrow(fonts, 1);
	*fp = font;

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.objects = arraycreate(sizeof(Object *), 8, nil);
	rich.views = arraycreate(sizeof(View), 8, nil);
	rich.text = arraycreate(sizeof(char), 4096, nil);

	rich.page.scroll = ZP;

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
			mouse(mctl, mv, &mmode);
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
				if (olast->dtext->count > 0) olast->dtext->count--;
				redraw(1);
				break;
			}
			if (rich.objects->count > 0) {
				int n;
				char *p;
				n = runelen(kv);

				qlock(rich.l);

				p = arraygrow(olast->dtext, n);

				runetochar(p, &kv);

				qunlock(rich.l);

				redraw(1);
			}
			if (kv == '\n') {
				Object *obj;

				obj = mkobjectftree(newobject(&rich, nil), fsctl->tree->root);

				qlock(rich.l);

				dv = arraycreate(sizeof(char), olast->dtext->n, nil);
				arraygrow(dv, olast->dtext->count);
				memcpy(dv->p, olast->dtext->p, dv->count);

				arraygrow(obj->dtext, olast->dtext->n);
				memcpy(obj->dtext->p, olast->dtext->p, olast->dtext->count);
				olast->dtext->count = 0;

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
mouse(Mousectl *mc, Mouse mv, int *mmode)
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
			rich.sel.n[0] = getsel(mv.xy);
			rich.sel.v[0] = getview(mv.xy);
			rich.sel.n[1] = rich.sel.n[0];
			rich.sel.v[1] = rich.sel.v[0];
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
			Object *obj;
			Array *dlink;
			v = getview(mv.xy);
			obj = nil;
			dlink = nil;
			if (v != nil) obj = v->obj;
			if (obj != nil) dlink = obj->dlink;
			if ((dlink != nil) && (dlink->n > 0)) {
				Array dv;
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
		rich.sel.n[1] = getsel(mv.xy);
		rich.sel.v[1] = getview(mv.xy);
		redraw(0);
	}
}

usize
getsel(Point p)
{
	View *v;
	long i;

	v = getview(p);

	p = addpt(subpt(p, rich.page.r.min), rich.page.scroll);

	if (v == nil) return 0;
	if (v->length == 0) return 0;

	for (i = 0; i < v->length; i++) {
		if (stringnwidth(v->obj->font, v->dp, i) >=
		  p.x - v->r.min.x)
			break;
	}
	return i - 1;
}

View *
getview(Point p)
{
	int i;
	View *vp;


	if (p.x < rich.page.r.min.x) p.x = rich.page.r.min.x;
	if (p.x > rich.page.r.max.x) p.x = rich.page.r.max.x;

	p = addpt(subpt(p, rich.page.r.min), rich.page.scroll);

	for (i = 0; i < rich.views->count; i++) {
			
		vp = arrayget(rich.views, i);

		if (ptinrect(p, vp->r) != 0)
			return vp;
	}
	return nil;
}

void
drawpage(Image *dst, Rich *rich)
{
	View *vp;
	int i;

	qlock(rich->l);
	for (i = 0; i < rich->views->count; i++) {
		vp = arrayget(rich->views, i);
		drawview(dst, vp);
	}
	qunlock(rich->l);
}

void
drawview(Image *dst, View *v)
{
	Image *bg1, *bg2, *bg3;
	Rectangle r, r1, r2, r3;
	View *vmin, *vmax;
	long nmin, nmax;

	if (rich.sel.v[0] < rich.sel.v[1]) {
		vmin = rich.sel.v[0];
		vmax = rich.sel.v[1];
		nmin = rich.sel.n[0];
		nmax = rich.sel.n[1];
	} else {
		vmin = rich.sel.v[1];
		vmax = rich.sel.v[0];
		nmin = rich.sel.n[1];
		nmax = rich.sel.n[0];
	}

	if (vmin == vmax) {
		if (rich.sel.n[0] < rich.sel.n[1]) {
			nmin = rich.sel.n[0];
			nmax = rich.sel.n[1];
		} else {
			nmin = rich.sel.n[1];
			nmax = rich.sel.n[0];
		}
	}

	r = rectaddpt(rectsubpt(v->r, rich.page.scroll),
	  rich.page.r.min);

	r1 = r;
	r1.max.x = r.min.x;
	r2 = r;
	r3 = r;
	r3.min.x = r.max.x;

	bg1 = Inormbg;
	bg2 = Inormbg;
	bg3 = Inormbg;

	if ((v > vmin) && (v < vmax)) {
		bg1 = Iselbg;
		bg2 = Iselbg;
		bg3 = Iselbg;
	}

	if (v == vmin) {
		r1.max.x = r.min.x + stringnwidth(v->obj->font, v->dp, nmin);
		r2.min.x = r1.max.x;
		bg1 = Inormbg;//Iselbg;
		bg2 = Iselbg;//Iselbg;
		bg3 = Iselbg;//Iselbg;
	}
	if (v == vmax) {
		r2.max.x = r.min.x + stringnwidth(v->obj->font, v->dp, nmax);
		r3.min.x = r2.max.x;
		bg1 = Iselbg;//Iselbg;
		bg2 = Iselbg;//Iselbg;
		bg3 = Inormbg;//Iselbg;
	}
	if (vmin == vmax) {
		bg1 = Inormbg;
		bg3 = Inormbg;//Iselbg;
	}

	draw(dst, r1, bg1, nil, ZP);
	draw(dst, r2, bg2, nil, ZP);
	draw(dst, r3, bg3, nil, ZP);

	if (v->obj->image != nil) {
		draw(dst, r, v->obj->image, nil, ZP);
		return;
	}

	stringn(dst, r.min, v->color, ZP,
	  v->obj->font, v->dp, v->length);
}

View *
viewadd(Array *views, Object *obj,
  char *dp, long len, Rectangle rprev)
{
	View *vp;
	Rectangle r;
	Point p;

	p = Pt(rprev.max.x, rprev.min.y);

	r = Rpt(p,
	  addpt(p, Pt(
	    stringnwidth(obj->font, dp, len),
	    (Dy(rprev) > obj->font->height) ?
	    Dy(rprev) : obj->font->height)));

	vp = arraygrow(views, 1);

	*vp = (View) {
		obj,
		dp,
		len,
		display->black,
		r,
	};

	return vp;
}

View *
generateviews(Rich *rich, Object *obj, View *v)
{
	Array *views;
	Rectangle rprev;
	char *sp, *p, *end;
	long n;

	enum {
		FL_NEW = 1,
		FL_TAB = 2,
		FL_EL  = 4,
		FL_NL  = 8,
	};

	views = rich->views;
	rprev = (v != nil) ? v->r : Rect(0, 0, 0, 0);
	end = obj->dtext->p + obj->dtext->count;
	sp = obj->dtext->p;
	
	for (p = sp, n = 0; p < end; p++, n++) {
		int fl;
		fl = 0;
		if (*p == '\t') fl = FL_NEW | FL_TAB;
		if (*p == '\n') fl = FL_NEW | FL_EL;
		if (rprev.max.x + stringnwidth(obj->font, p, n) >
		  Dx(rich->page.r)) {
			fl = FL_NEW | FL_NL;
		}
		
		/* TODO:
		 * if object has an image ... */

		if (fl & FL_NEW) {
			v = viewadd(views, obj, sp, n, rprev);
			rprev = v->r;
			sp = p + 1;
			n = -1;
		}
		if (fl & FL_TAB) {
			int tl;
			v = viewadd(views, obj, sp, 0, rprev);
			tl = stringwidth(font, "0") * 4;
			v->r.max.x = ((v->r.max.x / tl) + 1) * tl;
			rprev = v->r;
		}
		if (fl & FL_EL) {
			v = viewadd(views, obj, sp, 0, rprev);
			v->r.max.x = rich->page.r.max.x;
			rprev = v->r;
		}
		if (((v != nil) && (v->r.max.x >= rich->page.r.max.x)) ||
		  (fl & FL_NL)) {
			rprev = Rect(0, rprev.max.y, 0, rprev.max.y);
		}
	}
	v = viewadd(views, obj, sp, n, rprev);

	return v;
}

void
generatepage(Rich *rich, long n)
{
	Object **op;
	View *v;
	Array *views;
	long i;

	qlock(rich->l);

	views = rich->views;
	if (n < views->count) views->count = n;

	v = (views->count > 0) ?
	  arrayget(views, views->count -1) : nil;
	for (i = n; i < rich->objects->count; i++) {
		op = arrayget(rich->objects, i);
		v = generateviews(rich, *op, v);
	}
	rich->page.max = Pt(0, v->r.max.y);

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
newobject(Rich *rich, char *text)
{
	Object *obj, **op;
	qlock(rich->l);

	op = arraygrow(rich->objects, 1);

	if (rich->objects->count > 1) {
		Object **o1;
		o1 = arrayget(rich->objects, rich->objects->count - 2);
		*op = *o1;
		op = o1;
	}

	obj = mallocz(sizeof(Object), 1);

	*op = obj;

	obj->dtext = arraycreate(sizeof(char), 4096, nil);
	obj->dfont = arraycreate(sizeof(char), 4096, nil);
	obj->dlink = arraycreate(sizeof(char), 4096, nil);
	obj->dimage = arraycreate(sizeof(char), 4096, nil);

	if (text != nil) {
		char *p;
		p = arraygrow(obj->dtext, strlen(text));
		memcpy(p, text, strlen(text));

		p = arraygrow(rich->text, strlen(text));
		memcpy(p, text, strlen(text));
	};

	arraygrow(obj->dfont, strlen(font->name));
	memcpy(obj->dfont->p, font->name, strlen(font->name));

	obj->id = smprint("%ulld", rich->idcount);

	obj->font = font;

	obj->text = rich->text;
	obj->offset = rich->text->count;

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
	draw(screen, screen->r, Inormbg, nil, ZP);
	if ((rich.objects->count != 0) && (regen != 0)) {
		generatepage(&rich, 0);
	}
	drawpage(screen, &rich);
	drawscrollbar();

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

Array *
getseltext(Rich *rich)
{
	long n, nmin, nmax;
	View *vmin, *vmax;
	Object **o, **om, *omin, *omax;
	Array *d;

	d = malloc(sizeof(Array));

	vmin = rich->sel.v[0];
	vmax = rich->sel.v[1];
	nmin = rich->sel.n[0];
	nmax = rich->sel.n[1];
	if (vmin > vmax) {
		vmin = rich->sel.v[1];
		vmax = rich->sel.v[0];
		nmin = rich->sel.n[1];
		nmax = rich->sel.n[0];
	}

	if (vmin == nil) vmin = arrayget(rich->views, 0);
	if (vmax == nil) vmax = arrayget(rich->views, 0);

	if (vmin == vmax) {
		if (nmin > nmax) {
			n = nmin;
			nmin = nmax;
			nmax = n;
		}
		d->n = nmax - nmin;
		d->p = malloc(d->n + 1);
		d->p[d->n] = '\0';
		memcpy(d->p, vmin->dp + nmin, d->n);
		return d;
	}

	omin = vmin->obj;
	omax = vmax->obj;

	for (om = arrayget(rich->objects, 0); *om != omin; om++); 
	
	for (o = om, n = nmax - nmin; o[1] != omax; o++)
		n += (*o)->dtext->count;
	
	d->n = n;
	d->p = malloc(n + 1);
	d->p[n] = '\0';
	
	if ((*om)->dtext->p != nil)
		memcpy(d->p, (*om)->dtext->p + nmin,
		  (*om)->dtext->count - nmin);

	for (o = om + 1; o[1] != omax; o++) {
		if ((*o)->dtext->p != nil)
			memcpy(d->p + n, (*o)->dtext->p, (*o)->dtext->count);
		n += (*o)->dtext->count;
	}

	o++;

	if ((*o)->dtext->p != nil)
		memcpy(d->p + n, (*o)->dtext->p, nmax);

	return d;
}

void
mpaste(Rich *)
{
}

void
msnarf(Rich *rich)
{
	Array *d;
	int snarf;
	long n;
	n = 0;
	d = getseltext(rich);

	if (d->n > 0) {
		snarf = open("/dev/snarf", OTRUNC|OWRITE);
		if (snarf >= 0) {
			n = write(snarf, d->p, d->n); 
			close(snarf);
		}
	}
	if (n != d->n)
	fprint(2, "%s: msnarf: %r\n", argv0);

	free(d->p);
	free(d);
}

void
mplumb(Rich *)
{
}

