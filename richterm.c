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

	if (initdraw(0, 0, "richterm") < 0)
		sysfatal("%s: %r", argv0);

	rich.obj = malloc(sizeof(Object) * 4);
	rich.count = 4;
	rich.obj[0] = (Object){
		"text",
		"font=/lib/font/bit/terminus/unicode.14.font",
		"Hello\nworld!\n",
		strlen("Hello\nworld!\n")
	};
	rich.obj[1] = (Object){
		"text",
		"font=/lib/font/bit/terminus/unicode.18.font",
		"\nworld of hello\n",
		strlen("\nworld of hello\n")
	};
	rich.obj[2] = (Object){"text", "", "emerglerd\n", strlen("emerglerd\n")};
	rich.obj[3] = (Object){
		"text",
		"",
		"very long line very long line very long line\n",
		strlen("very long line very long line very long line\n")
	};

	rich.page = generatepage(screen->r, &rich);

	draw(screen, screen->r, display->white, nil, ZP);

	int i;
	for (i = 0; i < rich.page.count; i++){
		drawview(screen, &rich.page.view[i]);
	}
	flushimage(display, 1);
	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("%s: %r", argv0);
	if ((kctl = initkeyboard(nil)) == nil)
		sysfatal("%s: %r", argv0);

	// if (initdevfs() < 0) sysfatal("initdevfs failed: %r");
	// init /mnt fs for exposing internals

	// launch a subprocess from cmd passed on args
	// if args are empty, cmd = "rc"

	enum {MOUSE, RESIZE, KBD, NONE};
	Alt alts[4]={
		{mctl->c, &mv, CHANRCV},
		{mctl->resizec, rv, CHANRCV},
		{kctl->c, &kv, CHANRCV},
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
			draw(screen, screen->r, display->white, nil, ZP);
			for (i = 0; i < rich.page.count; i++){
				drawview(screen, &rich.page.view[i]);
			}
			flushimage(display, 1);
			break;
		case KBD:
			if (kv == 0x7f) threadexitsall(nil);
			break;
		case NONE:
			break;
		}
	}
}

void
drawview(Image *dst, View *v)
{
	draw(dst, v->r, display->white, nil, ZP);
	stringn(dst, v->r.min, display->black, ZP, v->font, v->dp, v->length);
}

Page
generatepage(Rectangle r, Rich *rich)
{
	#define BSIZE 4096

	char *bp, *sp, buf[BSIZE], *argc[2];
	Object *obj;
	int newline, ymax, argv;
	Point pt;
	Page page;

	page.view = nil;
	page.count = 0;
	pt = r.min;
	
	ymax = 0;
	
	obj = rich->obj;
	sp = obj->data;
	
	while (obj < rich->obj + rich->count) {
		View *v;
		char *brkp;
		
		newline = 0;
		page.count++;
		page.view = realloc(page.view, sizeof(View) * (page.count));
		v = page.view + page.count - 1;
		
		v->obj = obj;
		v->page = &page;
		v->font = font;
		
		// parse opts, don't like it here.
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

		if (newline != 0) {
			pt.x = r.min.x;
			pt.y = ymax;
		} else {
			pt.x = v->r.max.x;
		}
			
		if (v->length >= obj->count - 1) {

			obj++;
			sp = obj->data;
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