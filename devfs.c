#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "richterm.h"

Channel *objchan;

void
devfs_read(Req *r)
{
	respond(r, nil);
}

void
devfs_write(Req *r)
{
	Object obj;
	obj.type = strdup("text");
	obj.opts = strdup("");
	obj.count = r->ifcall.count + 1;
	obj.data = mallocz(obj.count, 1);
	memcpy(obj.data, r->ifcall.data, r->ifcall.count);
	send(objchan, &obj);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

int
initdevfs(Channel *chan)
{
	/* TODO: should set errstr */
	static Srv srv = {
		.read = devfs_read,
		.write = devfs_write,
	};
	objchan = chan;
	srv.tree = alloctree(nil, nil, 0600, nil);
	if (srv.tree == nil) return -1;
	if (createfile(srv.tree->root, "cons", nil, 0600, nil) == nil)
		return -1;
	threadpostmountsrv(&srv, "rtdev", nil, 0);
	return 0;
}
