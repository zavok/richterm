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

void
threadmain(int argc, char **argv)
{
	Mousectl *mctl;
	int rv[2];
	Mouse mv;
	View view;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	}ARGEND

	if (initdraw(0, 0, "richterm") < 0)
		sysfatal("initdraw failed: %r");
	if (initview(&view) < 0) sysfatal("initview failed: %r);
	draw(screen, screen->r, display->white, nil, ZP);
	flushimage(display, 1);
	if ((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse failed: %r");

	if (initdevfs() < 0) sysfatal("initdevfs failed: %r");
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
			draw(screen, screen->r, display->white, nil, ZP);
			flushimage(display, 1);
			break;
		case NONE:
			break;
		}
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-D] [cmd]\n", argv0);
	threadexitsall("usage");
}

int
initview(View *view)
{
	char *m = "Welcome to RichTerm!";
	view->count = 1;
	view->obj = malloc(sizeof(Obj));
	view->obj[0] = (Obj){ "text", nil, m, strlen(m) };
}
