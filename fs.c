#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "richterm.h"

File *new;

void
fs_read(Req *r)
{
	respond(r, nil);
}

void
fs_write(Req *r)
{
	respond(r, nil);
}

Fsctl *
initfs(void)
{
	Fsctl *fsctl;
	static Srv srv = {
		.read = fs_read,
		.write = fs_write,
	};
	fsctl = mallocz(sizeof(Fsctl), 1);
	fsctl->c = chancreate(sizeof(int), 0);
	srv.tree = alloctree("richterm", "richterm", DMDIR|555, nil);
	if (srv.tree == nil) return nil;
	new = createfile(srv.tree->root, "new", "richterm", 0666, nil);
	return fsctl;
}
