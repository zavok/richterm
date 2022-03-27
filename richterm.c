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
Image *Iscrollbar, *Ilink, *Inormbg, *Iselbg, *Itext;
Keyboardctl *kctl;
Mousectl *mctl;
Rich rich;
char *srvname;
int hostpid = -1;
Array *drawcache;

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

	elems = arraycreate(sizeof(Elem *), 128, nil);
	richdata = arraycreate(sizeof(char), 1024, nil);
	drawcache = arraycreate(sizeof(DrawState), 1024, nil);

	elemslinklist(elems);
	elemsupdatecache(elems);

	rich.selmin = 0;
	rich.selmax = 0;

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
				if (kv == 0x08) { /* backspace */
					if (rich.input < elems->count) {
						Elem *edisc;
						arrayget(elems, elems->count - 1, &edisc);
						if (edisc == nil) break;
						freeelem(edisc);
						arraydel(elems, elems->count - 1, 1);
						elemslinklist(elems);
						elemsupdatecache(elems);
					}
				} else {
					Elem *enew = mallocz(sizeof(Elem), 1);
					enew->type = TRune;
					enew->r = kv;
					enew->font = font;
					arraygrow(elems, 1, &enew);
					elemslinklist(elems);
					elemsupdatecache(elems);
				}
				if (kv == '\n') {
					Rune *r;
					char *s;
					r = getrunes(rich.input, elems->count);
					s = smprint("%S", r);

					arraygrow(richdata, 1, ".");
					arraygrow(richdata, strlen(s), s);
					arraygrow(richdata, 2, "n\n");

					rich.input = elems->count;

					Array *msg = arraycreate(sizeof(char), strlen(s), nil);
					arraygrow(msg, strlen(s), s);
					nbsend(consc, &msg);
					free(r);
					free(s);
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
		arraygrow(richdata, 1, ".");
		while((n = read(fd, buf, sizeof(buf))) > 0) {
			arraygrow(richdata, n, buf);
		}
		arraygrow(richdata, 1, "\n");
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
	Rune *r;
	char *s;
	if ((rich.selmin < rich.selmax) &&
	  ((fd = open("/dev/snarf", OWRITE)) > 0)) {
		r = getrunes(rich.selmin, rich.selmax);
		s = smprint("%S", r);
		n = write(fd, s, strlen(s));
		if (n < strlen(s)) fprint(2, "msnarf: %r\n");
		close(fd);
		free(r);
		free(s);
	}
}

void
mplumb(Rich *)
{
	char buf[1024];
	int pd;
	Plumbmsg m;
	Rune *r;
	char *s;
	if ((pd = plumbopen("send", OWRITE)) > 0) {
		r = getrunes(rich.selmin, rich.selmax);
		s = smprint("%S", r);
		m = (Plumbmsg) {
			"richterm",
			nil,
			getwd(buf, sizeof(buf)),
			"text",
			nil,
			strlen(s),
			s
		};
		plumbsend(pd, &m);
		close(pd);
		free(r);
		free(s);
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
	char *sp, *ep, *buf;


	type = str[0];
	sp = str + 1;
	if (n <= 1) return nil;
	ep = memchr(sp, '\n', n - 1);
	if (ep == nil) return nil;

	e->type = type;
	e->count = ep - sp;

	if (ep == sp) buf = nil;
	else {
		buf = mallocz(ep - sp + 1, 1);
		memcpy(buf, str + 1, ep - sp);
	}

	e->str = buf;

	switch(e->type) {
	case E_NL:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L'\n';
		if (buf != nil) free(buf);
		e->str = smprint("\n");
		break;
	case E_TAB:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L'\t';
		if (buf != nil) free(buf);
		e->str = smprint("\t");
		break;
	case E_SPACE:
		e->type = E_TEXT;
		e->count = 1;
		e->r = L' ';
		if (buf != nil) free(buf);
		e->str = smprint(" ");
		break;
	}
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
		e->type = TRune;
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
		e->font = font;
		dp = elemparse(e, dp, data->p + data->count - dp);
		if (dp == nil) break;
		if ((e->type == E_TEXT) && (e->count > 1)) {
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
			if (e->str != nil) link = strdup(e->str);
			else link = nil;
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
drawelems(void)
{
	DrawState ds;
	Elem *e;
	e = nil;
	ds.pos = Pt(rich.r.min.x, rich.r.min.y - rich.scroll);
	ds.nlpos = Pt(rich.r.min.x, ds.pos.y + font->height);

	for (ds.n = 0; ds.n < elems->count; ds.n++) {
		if (arrayget(elems, ds.n, &e) == nil)
			sysfatal("drawelems: failed to get elem");
		e->pos = ds.pos;
		drawelem(&ds, e);
		e->nlpos = ds.nlpos;
	}
	if (e != nil) rich.max = e->nlpos.y + rich.scroll - rich.r.min.y;
	else rich.max = 0;
}

Point
drawelem(DrawState *ds, Elem *e)
{
	if (e == nil) sysfatal("drawelem: e is nil");
	if (e->type != E_TEXT) return ds->pos;

	drawrune(ds, e);
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

	if (e->font == nil) {
		sysfatal("drawrune: e->font is nil! [%d]%c%C", ds->n, e->type, e->r);
	}

	fg = (e->link != nil) ? Ilink : Itext;
	bg = ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) ? Iselbg : Inormbg;

	if (ds->nlpos.y < ds->pos.y + e->font->height)
		ds->nlpos.y = ds->pos.y + e->font->height;

	pos = ds->pos;
	w = runestringnwidth(e->font, &e->r, 1);
	if (pos.x + w > rich.r.max.x) {
		pos = ds->nlpos;
		ds->nlpos.y = pos.y + e->font->height;
	}

	if ((pos.y >= rich.r.min.y) && (pos.y <= rich.r.max.y)) {
		runestringnbg(screen, pos, fg, ZP, e->font, &e->r, 1, bg, ZP);
	}
	pos.x += w;
	ds->pos = pos;
	return ds->pos;
}

Point
drawnl(DrawState *ds, Elem *e)
{
	if (ds->nlpos.y <= ds->pos.y) {
		ds->nlpos.y = ds->pos.y + e->font->height;
		ds->nlpos = ds->nlpos;
	}

	if ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) {
		draw(screen, Rpt(ds->pos, Pt(rich.r.max.x, ds->nlpos.y)), Iselbg, nil, ZP);
	}
	ds->pos = ds->nlpos;
	return ds->pos;
}

Point
drawtab(DrawState *ds, Elem *)
{
	int x, tabw;
	tabw = stringwidth(font, "0") * 4;
	x = (ds->pos.x - rich.r.min.x) / tabw;
	Point pos;
	pos = ds->pos;
	pos.x = rich.r.min.x + (x + 1) * tabw;

	if ((ds->n >= rich.selmin) &&
	  (ds->n < rich.selmax)) {
		draw(screen, Rpt(ds->pos, Pt(pos.x, ds->nlpos.y)), Iselbg, nil, ZP);
	}

	ds->pos = pos;
	return ds->pos;
}

void
insertfromcons(Array *a)
{
	int i;

	if (a->count == 0) return;

	qlock(a->l);

	arraygrow(richdata, 1, ".");

	for (i = 0; i < a->count; i++) {
		switch (a->p[i]) {
		case '\n':
			arraygrow(richdata, 4, "\nn\n.");
			break;
		default:
			arraygrow(richdata, 1, a->p + i);
		}
	}

	arraygrow(richdata, 1, "\n");

	qunlock(a->l);

	arrayfree(a);

	Rune *r = getrunes(rich.input, elems->count);
	for (i = 0; i < elems->count; i++) {
		Elem *e;
		arrayget(elems, 1, &e);
		freeelem(e);
	};
	elems->count = 0;

	parsedata(richdata, elems);
	rich.input = elems->count;
	for (i = 0; r[i] != L'\0'; i++) {
		Elem *e = mallocz(sizeof(Elem), 1);
		e->type = TRune;
		e->r = r[i];
		e->font = font;
		arraygrow(elems, 1, &e);
	}
	free(r);
	elemslinklist(elems);
	elemsupdatecache(elems);

	nbsend(redrawc, nil);
}

void
freeelem(Elem *e)
{
	if (e == nil) sysfatal("freeelem: elem is nil!");

	e->str  = realloc(e->str, 0);
	e->link = realloc(e->link, 0);
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

Rune *
getrunes(long start, long end)
{
	Rune *r = malloc(sizeof(Rune) * (end - start + 1));
	long i, n;
	Elem *e;
	for (n = 0, i = start; i < end; i++) {
		arrayget(elems, i, &e);
		if (e->type ==TRune) {
			r[n] = e->r;
			n++;
		}
	}
	r[n] = L'\0';
	return r;
}