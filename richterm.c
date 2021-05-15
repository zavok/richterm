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
#include "ui.h"

typedef struct Rich Rich;
struct Rich {
	Object *obj;
	long count;
	Page page;
};

typedef struct Fonts Fonts;
struct Fonts {
	Font **data;
	int size;
	int count;
};

int hostpid = -1;
Channel *pidchan;

Devfsctl *dctl;

Fonts fonts;

void generatepage(Rectangle, Rich *);
Font* getfont(Fonts *, char *);
void addfont(Fonts *, Font *);
void shutdown(void);
void send_interrupt(void);
void runcmd(void *);
void scroll(Point, Rich *);

void
runcmd(void *args)
{
	char **argv = args;
	char *cmd;
	
	rfork(RFNAMEG);

	if ((dctl = initdevfs()) == nil)
		sysfatal("initdevfs failed: %r");
	
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
usage(void)
{
	fprint(2, "usage: %s [-D] [cmd]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Object ov, *olast;
	Rich rich;
	int i;
	Mousectl *mctl;
	Keyboardctl *kctl;
	int rv[2];
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

	rich.obj = malloc(sizeof(Object) * 2);
	rich.count = 2;
	rich.obj[0] = (Object){
		"text",
		"font=/lib/font/bit/lucida/unicode.24.font",
		strdup("This is richterm\n"),
		strlen("This is richterm\n")
	};
	rich.obj[1] = (Object){
		"text",
		"font=/lib/font/bit/lucida/unicode.16.font",
		strdup("The future of textual interfacing\n"),
		strlen("The future of textual interfacing\n")
	};
	rich.page.scroll = ZP;
	rich.page.view = nil;
	generatepage(screen->r, &rich);

	draw(screen, screen->r, display->white, nil, ZP);
	drawpage(screen, &rich.page);
	flushimage(display, 1);

	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("%s: %r", argv0);
	if ((kctl = initkeyboard(nil)) == nil)
		sysfatal("%s: %r", argv0);

	// init /mnt fs for exposing internals

	// launch a subprocess from cmd passed on args
	// if args are empty, cmd = "rc"

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
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			generatepage(screen->r, &rich);
			draw(screen, screen->r, display->white, nil, ZP);
			drawpage(screen, &rich.page);
			flushimage(display, 1);
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
			olast = rich.obj + rich.count - 1;
			olast->count++;
			olast->data = realloc(olast->data, olast->count + 1);
			olast->data[olast->count - 1] = kv;
			olast->data[olast->count] = 0;
			generatepage(screen->r, &rich);
			draw(screen, screen->r, display->white, nil, ZP);
			drawpage(screen, &rich.page);
			flushimage(display, 1);
			nbsend(dctl->rc, &kv);
			break;
		case DEVFSWRITE:
			rich.count++;
			rich.obj = realloc(rich.obj, rich.count * sizeof(Object));
			rich.obj[rich.count - 1] = ov;

			generatepage(screen->r, &rich);
			draw(screen, screen->r, display->white, nil, ZP);

			for (i = 0; i < rich.page.count; i++){
				drawview(screen, &rich.page.view[i]);
			}
			flushimage(display, 1);

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
	for (i = 0; i < p->count; i++) {
		drawview(dst, p->view + i);
	}
}

void
drawview(Image *dst, View *v)
{
	Rectangle r;
	r = rectsubpt(v->r, v->page->scroll);
	draw(dst, r, display->white, nil, ZP);
	stringn(dst, r.min, display->black, ZP, v->font, v->dp, v->length);
}

void
generatepage(Rectangle r, Rich *rich)
{
	#define BSIZE 4096

	char *bp, *sp, buf[BSIZE], *argc[2];
	Object *obj;
	int newline, tab, ymax, argv;
	Point pt;
	Page *page;
	page = &rich->page;

	page->count = 0;
	page->scroll = rich->page.scroll;
	pt = r.min;
	ymax = 0;
	
	obj = rich->obj;
	sp = obj->data;
	
	while (obj < rich->obj + rich->count) {
		View *v;
		char *brkp;
		newline = 0;
		tab = 0;
		page->count++;
		page->view = realloc(page->view, sizeof(View) * (page->count));
		v = page->view + page->count - 1;
		
		v->obj = obj;
		v->page = &rich->page;
		v->font = font;
		
		/* 
		 * following code section parses opts
		 * TODO: I don't like how opts are implemented,
		 * think about rewriting it.
		 */
		strncpy(buf, obj->opts, BSIZE);
		for (bp = buf; (bp != nil) && (argv = tokenize(bp, argc, 2) > 0); bp = argc[1]){
			if (strstr(argc[0], "font") == argc[0]) {
				v->font = getfont(&fonts, argc[0] + 5);
			}
			if (argv == 1) break;
		}
		
		v->dp = sp;
		v->length = obj->count;
		
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
			tl = stringwidth(font, "0000");
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
			
		if (v->length >= obj->count - 1) {

			obj++;
			sp = obj->data;
		}
	}
	rich->page.max.y = ymax;
	rich->page.max.x = 0;

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
		fprint(2, "can't load font %s\n", name);
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

	draw(screen, screen->r, display->white, nil, ZP);
	drawpage(screen, &r->page);
	flushimage(display, 1);
}
