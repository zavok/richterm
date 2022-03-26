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

int getelem(Point);
void insertfromcons(Array *);
void mouse(Mousectl *, Mouse, int *);
void resize(void);
void runcmd(void *);
void scroll(Point, Rich *);
void send_interrupt(void);
void shutdown(void);

Array *elems;
Array *fonts;
Array *richdata;
Channel *pidchan, *redrawc, *insertc;
Elem *euser;
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg, *Itext;
Keyboardctl *kctl;
Mousectl *mctl;
Rich rich;
char *srvname;
int hostpid = -1;

void mpaste(Rich *);
void mplumb(Rich *);
void msnarf(Rich *);

char *mitems[] = {"paste", "snarf", "plumb", nil};
void (*mfunc[])(Rich *) = {mpaste, msnarf, mplumb, nil};

Menu mmenu = {
	.item = mitems,
	.gen = nil,
	.lasthit = 0,
};

void rfollow(void *);
void rsnarf(void *);
void rplumb(void *);

char *ritems[] = {"Follow", "Snarf", "Plumb", nil};
void (*rfunc[])(void *) = {rfollow, rsnarf, rplumb, nil};
int rsize = sizeof(ritems) / sizeof(*ritems);
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

	av = nil;
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
	  display, Rect(0,0,1,1), screen->chan, 1, DWhite);
	Iselbg = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, 0xBBBBBBFF);
	Ilink = allocimage(
	  display, Rect(0,0,1,1), screen->chan, 1, DBlue);

	Itext = display->black;

	fonts = arraycreate(sizeof(Font *), 2, nil);
	arraygrow(fonts, 1, &font);

	rich.l = mallocz(sizeof(QLock), 1);

	qlock(rich.l);

	rich.selmin = 0;
	rich.selmax = 0;

	qunlock(rich.l);

	redrawc = chancreate(sizeof(void *), 8);
	insertc = chancreate(sizeof(Array *), 8);

	generatesampleelems();
	rich.selmin = 10;
	rich.selmax = 100;

	resize();
	draw(screen, screen->r, Inormbg, nil, ZP);
	drawelems();
	drawscrollbar();
	flushimage(display, 1);

	if(rfork(RFENVG) < 0)
		sysfatal("rfork: %r");
	quotefmtinstall();
	atexit(shutdown);

	pidchan = chancreate(sizeof(int), 0);
	proccreate(runcmd, argv, 16 * 1024);
	hostpid = recvul(pidchan);

	threadsetname("main");

	void *ov;

	enum {MOUSE, RESIZE, REDRAW, INSERT, KBD, AEND};
	Alt alts[AEND + 1] = {
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{redrawc, &ov, CHANRCV},
		{insertc, &av, CHANRCV},
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
			nbsend(redrawc, nil);
			break;
		case REDRAW:

			while (nbrecv(redrawc, &ov) != 0);

			lockdisplay(display);
			draw(screen, screen->r, Inormbg, nil, ZP);
			drawelems();
			drawscrollbar();
			flushimage(display, 1);
			unlockdisplay(display);
			break;
		case INSERT:
			insertfromcons(av);
			break;
		case KBD:
			if (kv == 0x7f) shutdown(); /* delete */
			if (kv == 0xf00e) { /* d-pad up */
				scroll(
				  Pt(0, rich.scroll - Dy(screen->r) / 8),
				  &rich);
				break;
			}
			if (kv == 0xf800) { /* d-pad down */
				scroll(
				  Pt(0, rich.scroll + Dy(screen->r) / 8),
				  &rich);
				break;
			}
			if (kv == 0xf00f) { /* page up */
				scroll(
				  Pt(0, rich.scroll - Dy(screen->r) / 4),
				  &rich);
				break;
			}
			if (kv == 0xf013) { /* page down */
				scroll(
				  Pt(0, rich.scroll + Dy(screen->r) / 4),
				  &rich);
				break;
			}
			if (elems->count > 0) {
				char *str;
				int ul;
				Rune *R;
				Elem *e;

				if (kv == 0x08) { /* backspace */
					if ((euser->count == 0) || (euser->str == nil)) break;
					ul = utfnlen(euser->str, euser->count);
					R = mallocz(sizeof(Rune) * ul, 1);
					runesnprint(R, ul, "%s", euser->str);
					free(euser->str);
					euser->str = smprint("%S", R);
					euser->count = strlen(euser->str);
					free(R);
				} else if (kv == '\n') {
					Array *msg = arraycreate(sizeof(char), 0, nil);
					if (euser->str != nil) {
						arraygrow(msg, strlen(euser->str), euser->str);
						str = smprint("%c%s\n" "n\n", euser->type, euser->str);
					} else str = smprint("n\n");
					arraygrow(msg, 1, "\n");
					nbsend(consc, &msg);

					arraygrow(richdata, strlen(str), str);

					e = mallocz(sizeof(Elem), 1);
					e->type = E_NL;
					e->str = strdup("\n");
					e->count = 1;
					e->font = font;
					
					arraygrow(elems, 1, &e);

					euser = mallocz(sizeof(Elem), 1);
					euser->type = E_TEXT;
					euser->str = nil;
					euser->count = 0;
					euser->font = font;

					arraygrow(elems, 1, &euser);

					elemslinklist(elems);
				} else {
					if (euser->str != nil) {
						str = smprint("%s%C", euser->str, kv);
						free(euser->str);
					} else str = smprint("%C", kv);
					euser->str = str;
					euser->count = strlen(str);
				}
				nbsend(redrawc, nil);
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
		  Pt(0, rich.scroll - (mv.xy.y - rich.r.min.y)),
		  &rich);
		return;
	}
	if (mv.buttons == 16) {
		scroll(
		  Pt(0, rich.scroll + (mv.xy.y - rich.r.min.y)),
		  &rich);
		return;
	}

	if (*mmode == MM_NONE) {
		if (ptinrect(mv.xy, rich.rs) != 0)
			*mmode = MM_SCROLLBAR;
		if (ptinrect(mv.xy, rich.r) != 0)
			*mmode = MM_TEXT;
	}

	switch (*mmode) {
	case MM_SCROLLBAR:
		if (mv.buttons == 1) {
			scroll(
			  subpt(Pt(0, rich.scroll),
			  Pt(0, mv.xy.y - rich.r.min.y)),
			  &rich);
		} else if (mv.buttons == 2) {
			scroll(
			  Pt(0,
			    (mv.xy.y - rich.r.min.y) *
		  	    ((double)rich.max / Dy(rich.r))),
		 	  &rich);
		} else if (mv.buttons == 4) {
			scroll(
			  addpt(Pt(0, rich.scroll),
			  Pt(0, mv.xy.y - rich.r.min.y)),
			  &rich);
		}
		break;
	case MM_TEXT:
		if (mv.buttons == 1) {
			selstart = getelem(mv.xy);
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
			int n;
			Elem *e = nil;
			n = getelem(mv.xy);
			if (n >= 0) arrayget(elems, n, &e);
			if ((e != nil) && (e->link != nil)) {
				f = menuhit(3, mc, &rmenu, nil);
				if (f >= 0) {
					if (f >= rsize - 1) ruseract(f - rsize + 1);
					else rfunc[f](e);
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
			break;
		}
		if (mv.buttons == (1|4)) {
			break;
		}
		selend = getelem(mv.xy);
		if (selend == -1 ) selend = selstart;
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
	if (p.y < 0) p.y = 0;
	if (p.y > r->max) p.y = r->max;

	r->scroll = p.y;

	// qlock(rich.l);
	// TODO: update elements y positions ???
	// qunlock(rich.l);

	nbsend(redrawc, nil);
}

void
drawscrollbar(void)
{
	double D;
	Rectangle rs;
	D =  (double)rich.max / (double)Dy(rich.r);
	if (D != 0) {
		rs = rectaddpt(Rect(
		  0,  rich.scroll / D,
		  11, (rich.scroll + Dy(rich.r)) / D
		), rich.rs.min);
	} else {
		rs = rich.rs;
		rs.max.x--;
	};

	draw(screen, rich.rs, Iscrollbar, nil, ZP);
	draw(screen, rs, Inormbg, nil, ZP);
}

void
runcmd(void *args)
{
	char **argv = args;
	char *cmd, *syslib, *cputype;
	
	rfork(RFNAMEG);

	if ((initfs(srvname)) != 0)
		sysfatal("initfs failed: %r");

	bind("/mnt/richterm", "/dev/", MBEFORE);

	bind("/sys/lib/richterm/bin/rc/", "/bin", MAFTER);
	cputype = getenv("cputype");
	syslib = smprint("/sys/lib/richterm/bin/%s/", cputype);
	bind(syslib, "/bin", MAFTER);
	free(cputype);
	free(syslib);

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
	rich.rs = Rpt(
	  addpt(Pt(1,1), screen->r.min),
	  Pt(screen->r.min.x + 13, screen->r.max.y - 1)
	);
	rich.r = Rpt(
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
			arraygrow(richdata, n, buf);
		}
		if (n < 0) fprint(2, "mpaste: %r\n");
		close(fd);
		nbsend(redrawc, nil);
	}
}

void
msnarf(Rich *)
{
	int fd;
	long n;
	if ((rich.selmin < rich.selmax) &&
	  ((fd = open("/dev/snarf", OWRITE)) > 0)) {
		n = write(fd, arrayget(richdata, rich.selmin, nil),
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
			arrayget(richdata, rich.selmin, nil)
		};
		plumbsend(pd, &m);
		close(pd);
	}
}


char *
rgen(int n)
{
	if (n <  rsize - 1) return ritems[n];
	else return rusergen(n - rsize + 1);
}

void
rfollow(void *v)
{
	Elem *e = (Elem *)v;

	Array *a;
	a = arraycreate(sizeof(char), 1024, nil);
	if (a == nil) sysfatal("rfollow: arraycreate failed: %r");
	arraygrow(a, 5, "link ");
	arraygrow(a, strlen(e->link), e->link);
	arraygrow(a, 1, "\n");

	nbsend(ctlc, &a);
}

void
rsnarf(void *v)
{
	Elem *e = (Elem *)v;

	int fd;
	long n;
	if ((fd = open("/dev/snarf", OWRITE)) > 0) {
		n = write(fd, e->link, strlen(e->link));
		if (n < strlen(e->link)) fprint(2, "rsnarf: %r\n");
		close(fd);
	}
}

void
rplumb(void *v)
{
	Elem *e = (Elem *)v;

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
			strlen(e->link),
			e->link
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
	if (s == nil) {
		return;
	}
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
			ps = menubuf->p + i + 1;
			k++;
		}
	}
	if (k != f) return nil;
	i++;
	for (pe = ps; i < menubuf->count; i++, pe++) {
		if (*pe == '\n') break;
	}
	if (pe == '\0') return nil;
	if (ps == pe) return nil;
	memcpy(genbuf, ps, pe - ps);
	return genbuf;
}

char *
elemparse(Elem *e, char *str, long n)
{
	int type;
	char *sp, *ep;

	type = *str;
	sp = str + 1;
	if (n <= 1) return nil;
	ep = memchr(sp, '\n', n - 1);
	if (ep == nil) return nil;


	e->type = type;
	e->count = ep - sp;


	switch(e->type) {
	case E_NL:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L'\n';
	case E_TAB:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L'\t';
	case E_SPACE:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L' ';
	}

	if (e->count > 0) {
		e->str = malloc(e->count + 1);
		memcpy(e->str, sp, e->count);
		e->str[e->count] = '\0';
	} else e->str = nil;

	return ep + 1;
}

void
text2runes(char *str, Array *elems)
{
	if (str == nil) return;
	Rune *r = runesmprint("%s", str);
	Rune *rp;
	for (rp = r; *rp != L'\0'; rp++) {
		Elem *e = mallocz(sizeof(Elem), 1);
		e->type = E_TEXT;
		e->str = smprint("%C", *rp);
		e->count = strlen(e->str);
		e->r = *rp;
		arraygrow(elems, 1, &e);
	}
	free(r);
}

void
parsedata(Array *data, Array *elems)
{
	Elem *e;
	char *dp;

	qlock(data->l);

	dp = data->p;

	while (dp != nil) {
		e = mallocz(sizeof(Elem), 1);
		dp = elemparse(e, dp, data->p + data->count - dp);
		if (dp == nil) break;
		if (e->type == E_TEXT) {
			text2runes(e->str, elems);
			freeelem(e);
		} else arraygrow(elems, 1, &e);
	}

	qunlock(data->l);
}

void
elemslinklist(Array *elems)
{
	Elem *e, *eold;
	long i;

	e = nil;

	for (i = 0; i < elems->count; i++) {
		eold = e;
		arrayget(elems, i, &e);

		e->prev = eold;
		if (eold != nil) eold->next = e;
	}
}

void
elemsupdatecache(Array *elems)
{
	Elem *e;
	char *link;
	Font *fnt;
	long i;

	link = nil;
	fnt = font;

	for (i = 0; i < elems->count; i++) {
		arrayget(elems, i, &e);
		switch(e->type) {
		case E_LINK:
			link = e->str;
			break;
		case E_FONT:
			if (e->str != nil) fnt = getfont(fonts, e->str);
			else fnt = font;
			break;
		case E_IMAGE:
			/* load image
			 * e->image = image
			 */
			break;
		}

		e->link = link;
		e->font = fnt;
	}
}

void
generatesampleelems(void)
{
	elems = arraycreate(sizeof(Elem *), 128, nil);
	richdata = arraycreate(sizeof(char), 1024, nil);

	euser = mallocz(sizeof(Elem), 1);
	euser->type = E_TEXT;
	euser->str = nil;
	euser->count = 0;

	arraygrow(elems, 1, &euser);

	elemslinklist(elems);
	elemsupdatecache(elems);
}

void
drawelems(void)
{
	long i;
	DrawState ds;
	Elem *e;
	e = nil;
	ds.pos = rich.r.min;
	ds.pos.y -= rich.scroll;
	for (i = 0; i < elems->count; i++) {
		ds.n = i;
		if (arrayget(elems, i, &e) == nil)
			sysfatal("drawelems: failed to get elem");
		e->pos = ds.pos;
		drawelem(&ds, e);
	}
	if (e != nil) rich.max = e->nlpos.y + rich.scroll - rich.r.min.y;
	else rich.max = 0;
}

Point
drawelem(DrawState *ds, Elem *e)
{
	Point (*drawp)(DrawState *, Elem *);

	static const Point (*dtable[256])(DrawState *, Elem *) = {
		[E_NOOP]  drawnoop,
		[E_TEXT]  drawrune,
		[E_FONT]  drawnoop,
		[E_LINK]  drawnoop,
		[E_IMAGE] drawnoop,
		[E_NL]    drawnoop,
		[E_TAB]   drawnoop,
		[E_SPACE] drawnoop,
	};

	if (e == nil) sysfatal("drawelem: e is nil");

	e->nlpos = (e->prev != nil) ?
		e->prev->nlpos :
		Pt(rich.r.min.x, rich.r.min.y + e->font->height - rich.scroll);

	ds->nlpos = e->nlpos;

	drawp = dtable[e->type];

	if (drawp == nil) {
		fprint(2, "drawelem: unknown elem type: 0x%uhhx '%c'\n", e->type, e->type);
		e->type = E_NOOP;
		drawp = drawnoop;
	}

	ds->pos = drawp(ds, e);
	return ds->pos;
}

Point
drawrune(DrawState *ds, Elem *e)
{
	if (e->r == L'\t') return drawtab(ds, e);
	if (e->r == L'\n') return drawnl(ds, e);

	Point pos;
	Image *fg, *bg;
	int w;

	if (e->font == nil) sysfatal("drawrune: e->font is nil!");

	fg = (e->link != nil) ? Ilink : Itext;
	bg = ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) ? Iselbg : Inormbg;

	if (e->nlpos.y < e->pos.y + e->font->height)
		e->nlpos.y = e->pos.y + e->font->height;

	pos = e->pos;
	w = runestringnwidth(e->font, &e->r, 1);
	if (pos.x + w > rich.r.max.x) {
		pos = e->nlpos;
		e->nlpos.y = pos.y + e->font->height;
	}

	if ((pos.y >= rich.r.min.y) && (pos.y <= rich.r.max.y)) {
		runestringnbg(screen, pos, fg, ZP, e->font, &e->r, 1, bg, ZP);
	}
	pos.x += w;
	return pos;
}

Point
drawnl(DrawState *ds, Elem *e)
{
	if (e->nlpos.y <= e->pos.y) {
		e->nlpos.y = e->pos.y + e->font->height;
		ds->nlpos = e->nlpos;
	}

	if ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) {
		draw(screen, Rpt(ds->pos, Pt(rich.r.max.x, ds->nlpos.y)), Iselbg, nil, ZP);
	}

	return e->nlpos;
}

Point
drawtab(DrawState *ds, Elem *e)
{
	int x, tabw;
	tabw = stringwidth(font, "0") * 4;
	x = (e->pos.x - rich.r.min.x) / tabw;
	Point pos;
	pos = e->pos;
	pos.x = rich.r.min.x + (x + 1) * tabw;

	if ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) {
		draw(screen, Rpt(ds->pos, Pt(pos.x, ds->nlpos.y)), Iselbg, nil, ZP);
	}

	return pos;
}

Point
drawnoop(DrawState *, Elem *e)
{
	return e->pos;
}

void
insertfromcons(Array *a)
{
	int i, nl;
	qlock(a->l);
	nl = 1;
	for (i = 0; i < a->count; i++) {
		if (nl != 0) {
			arraygrow(richdata, 1, ".");
			nl = 0;
		}

		switch (a->p[i]) {
		case ' ':
			arraygrow(richdata, 3, "\ns\n");
			nl = 1;
			break;
		case '\t':
			arraygrow(richdata, 3, "\nt\n");
			nl = 1;
			break;
		case '\n':
			arraygrow(richdata, 3, "\nn\n");
			nl = 1;
			break;
		default:
			arraygrow(richdata, 1, a->p + i);
		}
	}
	qunlock(a->l);

	arrayfree(a);

	// TODO: this is not how things should be done!
	// elems should be cleaned properly,
	// or even better only append fresh data instead of reparsing everything
	elems->count = 0;
	parsedata(richdata, elems);
	arraygrow(elems, 1, &euser);
	elemslinklist(elems);
	elemsupdatecache(elems);

	nbsend(redrawc, nil);
}

void
freeelem(Elem *e)
{
	e->str = realloc(e->str, 0);
	e->count = 0;
	e->next = nil;
	e->prev = nil;
	e->link = realloc(e->link, 0);
	if (e->image != nil) freeimage(e->image);
	e->font = nil;
	e->pos = ZP;
	e->nlpos = ZP;
}

int
getelem(Point xy)
{
	int i;
	Elem *e;
	Point sp, np, ep;
	for (i = 0; i < elems->count; i++) {
		arrayget(elems, i, &e);
		sp = e->pos;
		np = e->nlpos;
		ep = (e->next != nil) ? e->next->pos : Pt(rich.r.max.x, sp.y);
		if (
		  (xy.y >= sp.y) &&
		  (xy.y <  np.y) &&
		  (xy.x >= sp.x) &&
		  (xy.x <  ep.x)
		) return i;
	}
	return -1;
}
