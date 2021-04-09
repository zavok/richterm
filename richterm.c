#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include <9p.h>

void usage(void);

void
threadmain(int argc, char **argv)
{
	Mousectl *mctl;
	int rv[2];
	Mouse mv;
	ARGBEGIN{
	default:
		usage();
	}ARGEND

	// init graphics and I/O
	if (initdraw(0, 0, "richterm") < 0) sysfatal("initdraw failed: %r");
	draw(screen, screen->r, display->white, nil, ZP);
	flushimage(display, 1);
	mctl = initmouse(nil, screen);
	if (mctl == nil) sysfatal("initmouse failed: %r");
	
	// init /dev fs for console
	
	// init /mnt fs for exposing internals
	// even processing loop
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
			if (getwindow(display, Refnone) < 0) sysfatal("resize failed: %r");
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
	fprint(2, "usage: %s [cmd]\n", argv0);
	threadexitsall("usage");
}
