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

Page generatepage(Rectangle, Rich *);

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
	rich.obj[0] = (Object){"text", nil, "hello ", strlen("hello ")};
	rich.obj[1] = (Object){"text", nil, "world",  strlen("world")};
	rich.obj[2] = (Object){"text", nil, "!",      strlen("!")};
	rich.obj[3] = (Object){"text", nil, "\n",     strlen("\n")};

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
			draw(screen, screen->r, display->black, nil, ZP);
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
	string(dst, v->r.min, display->black, ZP, font, buf);
}

Page
generatepage(Rectangle r, Rich *rich)
{
	int i;
	Point pt;
	Page page;
	page.view = nil;
	page.count = 0;
	pt = r.min;
	for (i = 0; i < rich->count; i++) {
		Object *obj;
		View view;
		obj = &rich->obj[i];
		page.count++;
		page.view = realloc(page.view, sizeof(View) * (page.count));
		view.obj = obj;
		view.page = &page;
		view.dp = obj->data;
		view.r = Rpt(pt, addpt(pt, Pt(100,20)));
		page.view[page.count -1] = view;
		pt.y = view.r.max.y;
	}
	return page;
}
