#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
devfs_read(Req *r)
{
	respond(r, nil);
}

int
initdevfs(void)
{
	/* TODO: should set errstr */
	static Srv srv;
	srv.read = devfs_read;
	srv.tree = alloctree(nil, nil, 0600, nil);
	if (srv.tree == nil) return -1;
	if (createfile(srv.tree->root, "cons", nil, 0600, nil) == nil)
		return -1;
	threadpostmountsrv(&srv, "octreedev", "/dev", RFPROC);
	return 0;
}
