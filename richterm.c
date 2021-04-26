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

Fonts fonts;

Page generatepage(Rectangle, Rich *);
Font* getfont(Fonts *, char *);
void addfont(Fonts *, Font *);

void
usage(void)
{
	fprint(2, "usage: %s [-D] [cmd]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	Rich rich;
	Mousectl *mctl;
	int rv[2];
	Mouse mv;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	} ARGEND

	if (initdraw(0, 0, "richterm") < 0)
		sysfatal("initdraw failed: %r");

	//if (initview(&view) < 0) sysfatal("initview failed: %r);
	rich.obj = malloc(sizeof(Object) * 4);
	rich.count = 4;
	rich.obj[0] = (Object){
		"text",
		"font=/lib/font/bit/terminus/unicode.14.font",
		"Hello ",
		strlen("Hello ")
	};
	rich.obj[1] = (Object){
		"text",
		"font=/lib/font/bit/terminus/unicode.18.font",
		" world",
		strlen(" world")
	};
	rich.obj[2] = (Object){"text", "", "!",      strlen("!")};
	rich.obj[3] = (Object){"text", "", "\t\n",     strlen("\t\n")};

	rich.page = generatepage(screen->r, &rich);

	draw(screen, screen->r, display->black, nil, ZP);

	int i;
	for (i = 0; i < 4; i++){
		drawview(screen, &rich.page.view[i]);
	}
	flushimage(display, 1);
	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse failed: %r");

	// if (initdevfs() < 0) sysfatal("initdevfs failed: %r");
	// init /mnt fs for exposing internals

	// launch a subprocess from cmd passed on args
	// if args are empty, cmd = "rc"

	enum {MOUSE, RESIZE, NONE};
	Alt alts[3]={
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{nil, nil, CHANEND},
	};
	for (;;) {
		switch(alt(alts)) {
		case MOUSE:
			break;
		case RESIZE:
			if (getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			rich.page = generatepage(screen->r, &rich);
			draw(screen, screen->r, display->black, nil, ZP);
			for (i = 0; i < 4; i++){
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
drawview(Image *dst, View *v)
{
	int w, n;
	char buf[4096];
	w = Dx(v->r);
	draw(dst, v->r, display->white, nil, ZP);
	for (n = 0; stringnwidth(font, v->dp, n) < w; n++)
		if (n >= 4096) break;
	memcpy(buf, v->dp, n);
	buf[n] = '\0';
	string(dst, v->r.min, display->black, ZP, v->font, buf);
}

Page
generatepage(Rectangle r, Rich *rich)
{
	#define BSIZE 4096

	char *bp, buf[BSIZE], *argc[2];
	Object *obj;
	View view;
	int newline, ymax, i, argv;
	Point pt;
	Page page;

	page.view = nil;
	page.count = 0;
	pt = r.min;
	for (ymax = 0, i = 0; i < rich->count; i++) {
		// TODO: set newline=1 when there's \n character
		// or when we hit the edge of the window
		newline = 0;
		obj = &rich->obj[i];

		page.count++;
		page.view = realloc(page.view, sizeof(View) * (page.count));

		view.obj = obj;
		view.page = &page;
		view.font = font;
		view.dp = obj->data;
		view.length = strlen(view.dp);
		strncpy(buf, obj->opts, BSIZE);
		for (bp = buf; (bp != nil) && (argv = tokenize(bp, argc, 2) > 0); bp = argc[1]){
			if (strstr(argc[0], "font") == argc[0]) {
				view.font = getfont(&fonts, argc[0] + 5);
			}
			if (argv == 1) break;
		}
		view.r = (Rectangle){
			pt,
			addpt(pt, Pt(stringnwidth(view.font, view.dp, view.length),
				view.font->height))
		};

		page.view[page.count -1] = view;
		ymax = (ymax > view.r.max.y) ? ymax : view.r.max.y;
		if (newline == 0) pt.x = view.r.max.x;
		else {
			pt.x = r.min.x;
			pt.y += ymax;
			ymax = 0;
		}
	}
	return page;
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