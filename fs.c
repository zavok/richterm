#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "richterm.h"

File *new;

void
fs_read(Req *r)
{
	File *f;
	Fsctl *fsctl;
	f = r->fid->file;
	fsctl = f->aux;
	if (f == new) {
		respond(r, "not implemented");
	}
	else respond(r, "what");
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
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	if (srv.tree == nil) return nil;
	fsctl->tree = srv.tree;
	new = createfile(srv.tree->root, "new", "richterm", 0666, fsctl);
	if (new == nil) return nil;
	threadpostmountsrv(&srv, nil, "/mnt/richterm", MREPL);
	return fsctl;
}
