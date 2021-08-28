#include <u.h>
#include <libc.h>
#include <plumb.h>
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

Rich rich;
int hostpid = -1;
Channel *pidchan, *redrawc, *insertc;
Mousectl *mctl;
Keyboardctl *kctl;
Array *fonts;
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg, *Itext;

char *srvname;

void mpaste(Rich *);
void msnarf(Rich *);
void mplumb(Rich *);

char *mitems[] = {"paste", "snarf", "plumb", nil};
void (*mfunc[])(Rich *) = {mpaste, msnarf, mplumb, nil};

Menu mmenu = {
	.item = mitems,
	.gen = nil,
	.lasthit = 0,
};

void rfollow(Object *);
void rsnarf(Object *);
void rplumb(Object *);

char *ritems[] = {"Follow", "Snarf", "Plumb", nil};
void (*rfunc[])(Object *) = {rfollow, rsnarf, rplumb, nil};
char * rgen(int);

Menu rmenu = {
	.item = nil,
	.gen = rgen,
	.lasthit = 0,
};

char * rusergen(int);
void ruseract(int);

Menu rusermenu = {
	.item = nil,
	.gen = rusergen,
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
	fprint(2, "usage: %s [-D] [-s srvname] [cmd [args]]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int rv[2], mmode;
	Mouse mv;
	Rune kv;
	Object *ov, *obj;
	Array *av;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	mmode = MM_NONE;

	if (initdraw(0, 0, argv0) < 0)
		sysfatal("%s: %r", argv0);
	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("%s: %r", argv0);
	if ((kctl = initkeyboard(nil)) == nil)
		sysfatal("%s: %r", argv0);

	display->locking = 1;
	unlockdisplay(display);

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
	arraygrow(fonts, 1, &font);

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.text = arraycreate(sizeof(char), 4096, nil);
	rich.objects = arraycreate(sizeof(Object *), 8, nil);

	olast = objectcreate();
	arraygrow(rich.objects, 1, &olast);

	rich.page.scroll = ZP;

	rich.selmin = 0;
	rich.selmax = 0;

	qunlock(rich.l);

	redrawc = chancreate(sizeof(Object *), 8);
	insertc = chancreate(sizeof(Array *), 8);

	resize();
	draw(screen, screen->r, Inormbg, nil, ZP);
	drawscrollbar();
	flushimage(display, 1);

	if(rfork(RFENVG) < 0)
		sysfatal("rfork: %r");
	quotefmtinstall();
	atexit(shutdown);

	pidchan = chancreate(sizeof(int), 0);
	proccreate(runcmd, argv, 16 * 1024);
	hostpid = recvul(pidchan);

	enum {MOUSE, RESIZE, REDRAW, INSERT, KBD, AEND};
	Alt alts[AEND + 1] = {
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{redrawc, &ov, CHANRCV},
		{insertc, &av, CHANRCV},
		{kctl->c, &kv, CHANRCV},
		{nil, nil, CHANEND},
	};

	threadsetname("main");

	for (;;) {
		switch(alt(alts)) {
		case MOUSE:
			mouse(mctl, mv, &mmode);
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			resize();
			nbsend(redrawc, nil);
			break;
		case REDRAW:
			lockdisplay(display);
			draw(screen, screen->r, Inormbg, nil, ZP);
			redraw(ov);
			drawscrollbar();
			flushimage(display, 1);
			unlockdisplay(display);
			break;
		case INSERT:
			obj = objectcreate();
			mkobjectftree(obj, fsroot);
			objinsertbeforelast(obj);
			objsettext(obj, arrayget(av, 0, nil), av->count);
			arrayfree(av);
			nbsend(redrawc, &obj);
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
				if (rich.text->count > olast->offset) {
					rich.text->count--;
					nbsend(redrawc, &olast);
				}
				break;
			}
			if (rich.objects->count > 0) {
				Object *obj;
				int n;

				obj = nil;
				n = runelen(kv);

				qlock(rich.l);

				arraygrow(rich.text, n, &kv);

				qunlock(rich.l);

				if (kv == '\n') {
					Array *dv;

					qlock(rich.l);
	
					dv = arraycreate(sizeof(char),
					  rich.text->count - olast->offset, nil);
					arraygrow(dv, rich.text->count - olast->offset,
					  arrayget(rich.text, olast->offset, nil));

					nbsend(consc, &dv);

					/* dv is freed on recv end */

					qunlock(rich.l);
					
					obj = objectcreate();
					mkobjectftree(obj, fsroot);
					objinsertbeforelast(obj);
					olast->offset = rich.text->count;
				}
				nbsend(redrawc, &obj);
			}
			break;
		case AEND:
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
			nbsend(redrawc, nil);
			*mmode = MM_SELECT;
		} else if (mv.buttons == 2) {
			int f;
			f = menuhit(2, mc, &mmenu, nil);
			if (f >= 0) mfunc[f](&rich);
			*mmode = MM_NONE;
		} else if (mv.buttons == 4) {
			int f;
			Object *obj;
			obj = getobj(mv.xy);
			if ((obj != nil) && (obj->dlink->count > 0)) {
				f = menuhit(3, mc, &rmenu, nil);
				if (f >= 0) {
					if (f >= sizeof(ritems) - 1) ruseract(f - 2);
					else rfunc[f](obj);
				}
			} else if (menubuf->count > 0) {
				f = menuhit(3, mc, &rusermenu, nil);
				if (f >= 0) ruseract(f);
			}
			*mmode = MM_NONE;
		}
		break;
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
		nbsend(redrawc, nil);
	}
}

Font *
getfont(Array *fonts, char *name)
{
	int i;
	Font *newfont, *fnt;
	for (i = 0; i < fonts->count; i++){
		arrayget(fonts, i, &fnt);
		if (strcmp(fnt->name, name) == 0) return fnt;
	}
	if ((newfont = openfont(display, name)) == nil) {
		fprint(2, "%r\n");
		newfont = font;
	} else {
		arraygrow(fonts, 1, &newfont);
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

	nbsend(redrawc, nil);
}

Faux *
fauxalloc(Object *obj, Array *data, char * (*read)(Req *), char * (*write)(Req *))
{
	Faux *aux;
	aux = mallocz(sizeof(Faux), 1);
	*aux = (Faux) {obj, data, read, write};
	return aux;
}

Object *
objectcreate(void)
{
	Object *obj;
	obj = mallocz(sizeof(Object), 1);

	obj->font = font;
	obj->dlink = arraycreate(sizeof(char), 4096, nil);
	obj->dimage = arraycreate(sizeof(char), 4096, nil);
	return obj;
}

void
objectfree(Object *obj)
{
	arrayfree(obj->dlink);
	arrayfree(obj->dimage);
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

	if ((initfs(srvname)) != 0)
		sysfatal("initfs failed: %r");

	bind("/mnt/richterm", "/dev/", MBEFORE);

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
mpaste(Rich *)
{
	int fd;
	long n;
	char buf[4096];
	if ((fd = open("/dev/snarf", OREAD)) > 0) {
		while((n = read(fd, buf, sizeof(buf))) > 0) {
			arraygrow(rich.text, n, buf);
		}
		if (n < 0) fprint(2, "mpaste: %r\n");
		close(fd);
		nbsend(redrawc, &olast);
	}
}

void
msnarf(Rich *)
{
	int fd;
	long n;
	if ((rich.selmin < rich.selmax) &&
	  ((fd = open("/dev/snarf", OWRITE)) > 0)) {
		n = write(fd, arrayget(rich.text, rich.selmin, nil),
		  rich.selmax - rich.selmin);
		if (n < rich.selmax - rich.selmin) fprint(2, "msnarf: %r\n");
		close(fd);
	}
}

void
mplumb(Rich *)
{
	char buf[1024];
	int pd;
	Plumbmsg m;
	if ((pd = plumbopen("send", OWRITE)) > 0) {
		m = (Plumbmsg) {
			"richterm",
			nil,
			getwd(buf, sizeof(buf)),
			"text",
			nil,
			rich.selmax - rich.selmin,
			arrayget(rich.text, rich.selmin, nil)
		};
		plumbsend(pd, &m);
		close(pd);
	}
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
	Image *bg, *fg;
	Rune r;
	char *p;

	bg = ((*n >= rich.selmin) && (*n < rich.selmax)) ?
		Iselbg : Inormbg;

	fg = (obj->dlink->count > 0) ? Ilink : Itext;

	p = arrayget(rich.text, *n, nil);

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
			_drawchar(r, *cur, obj->font, fg, bg);
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

	obj->nextlinept = Pt(0, cur->y + obj->font->height);

	if ((obj->prev != nil) && (cur->x != 0)) {
		if (obj->nextlinept.y < obj->prev->nextlinept.y)
			obj->nextlinept.y = obj->prev->nextlinept.y;
	}

	if (obj->image != nil) {
		Rectangle r;
		r = rectaddpt(obj->image->r,
		  subpt(addpt(*cur, rich.page.r.min), rich.page.scroll));
		draw(screen, r, obj->image, nil, ZP);
		cur->x += Dx(r);
		if (cur->y + Dy(r) > obj->nextlinept.y)
			obj->nextlinept.y = cur->y + Dy(r);
	}

	obj->startpt = *cur;

	if (obj->next == nil) n = rich.text->count;
	else n = obj->next->offset;

	for (i = obj->offset; i < n;) {
		drawchar(obj, &i, cur);
	}
	obj->endpt = *cur;
}

void
redraw(Object *)
{
	/* TODO: only redraw starting from arg-supplied *obj */
	/* TODO: flush all messages in redrawc channel before proceeding */

	Point cur;
	Object *obj;
	long i;

	obj = nil;
	cur = ZP;
	qlock(rich.l);
	for (i = 0; i < rich.objects->count; i++) {
		if (arrayget(rich.objects, i, &obj) == nil)
			sysfatal("redraw: %r");
		drawobject(obj, &cur);
	}
	rich.page.max = cur;
	qunlock(rich.l);
}

Object *
getobj(Point xy)
{
	long i;
	Object *obj;
	Point prevlinept;
	xy = subpt(xy, subpt(rich.page.r.min, rich.page.scroll));
	for (i = 0; i < rich.objects->count; i++) {
		arrayget(rich.objects, i, &obj);
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

void
objinsertbeforelast(Object *obj)
{
	qlock(rich.l);
	obj->offset = olast->offset;
	arrayinsert(rich.objects, rich.objects->count - 1, 1, &obj);
	obj->next = olast;
	obj->prev = olast->prev;
	if (olast->prev != nil) olast->prev->next = obj;
	olast->prev = obj;
	qunlock(rich.l);
}

long
objtextlen(Object *obj)
{
	long n;
	n = (obj->next == nil) ? rich.text->count : obj->next->offset;
	return n - obj->offset;
}

void
objsettext(Object *obj, char *data, long count)
{
	long n, m, dn;
	char *p, *pe;

	n = objtextlen(obj);
	m = count;
	dn = m - n;

	if (dn > 0) arraygrow(rich.text, dn, nil);
	else rich.text->count += dn;

	p = arrayget(rich.text, obj->offset, nil);
	pe = arrayget(rich.text, rich.text->count - dn, nil);

	qlock(rich.text->l);
	if (p != nil) memcpy(p + m, p + n,  pe - p);
	else p = pe;
	memcpy(p, data, count);
	qunlock(rich.text->l);

	qlock(rich.objects->l);
	if (obj->ftext != nil) obj->ftext->length = count;
	for (obj = obj->next; obj != nil; obj = obj->next) {
		obj->offset += dn;
	}
	qunlock(rich.objects->l);
}

char *
rgen(int n)
{
	if (n < 3) return ritems[n];
	else return rusergen(n - 3);
}

void
rfollow(Object *obj)
{
	Array *a;
	a = arraycreate(sizeof(char), 1024, nil);
	arraygrow(a, 5, "link ");
	arraygrow(a, obj->dlink->count, arrayget(obj->dlink, 0, nil));
	arraygrow(a, 1, "\n");

	nbsend(ctlc, &a);
}

void
rsnarf(Object *obj)
{
	int fd;
	long n;
	if ((fd = open("/dev/snarf", OWRITE)) > 0) {
		n = write(fd, arrayget(obj->dlink, 0, nil),
		  obj->dlink->count);
		if (n < obj->dlink->count) fprint(2, "rsnarf: %r\n");
		close(fd);
	}
}

void
rplumb(Object *obj)
{
	char buf[1024];
	int pd;
	Plumbmsg m;
	if ((pd = plumbopen("send", OWRITE)) > 0) {
		m = (Plumbmsg) {
			"richterm",
			nil,
			getwd(buf, sizeof(buf)),
			"text",
			nil,
			obj->dlink->count,
			arrayget(obj->dlink, 0, nil)
		};
		plumbsend(pd, &m);
		close(pd);
	}
}

void
ruseract(int f)
{
	Array *a;
	char *s;
	s = rusergen(f);
	a = arraycreate(sizeof(char), 2048, nil);
	arraygrow(a, 5, "menu ");
	arraygrow(a, strlen(s), s);
	arraygrow(a, 1, "\n");
	nbsend(ctlc, &a);
}



char *
rusergen(int f)
{
	static char genbuf[1024];
	int i, k;
	char *ps, *pe;
	memset(genbuf, 0, sizeof(genbuf));
	ps = menubuf->p;
	for (k = 0, i = 0; (k != f) && (i < menubuf->count); i++) {
		if (menubuf->p[i] == '\n') {
			ps = menubuf->p + i;
			k++;
		}
	}
	if (k != f) return nil;
	ps++; i++;
	for (pe = ps; i < menubuf->count; i++, pe++) {
		if (*pe == '\n') break;
	}
	if (pe == '\0') return nil;
	if (ps == pe) return nil;
	memcpy(genbuf, ps, pe - ps - 1);
	return genbuf;
}
