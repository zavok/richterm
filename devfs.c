#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "richterm.h"

void
devfs_read(Req *r)
{
	respond(r, nil);
}

void
devfs_write(Req *r)
{
	Devfsctl *dctl;
	Object obj;
	dctl = r->fid->file->aux;
	if (dctl == nil) sysfatal("dctl is nil");
	if (dctl->wc == nil) sysfatal("dctl->wc is nil");
	obj.type = strdup("text");
	obj.opts = strdup("");
	obj.count = r->ifcall.count + 1;
	obj.data = mallocz(obj.count, 1);
	memcpy(obj.data, r->ifcall.data, r->ifcall.count);
	send(dctl->wc, &obj);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

Devfsctl *
initdevfs(void)
{
	Devfsctl *dctl;
	static Srv srv = {
		.read = devfs_read,
		.write = devfs_write,
	};
	dctl = mallocz(sizeof(Devfsctl), 1);
	
	dctl->wc = chancreate(sizeof(Object), 0);
	srv.tree = alloctree(nil, nil, 0600, nil);
	if (srv.tree == nil)
		return nil;
	if (createfile(srv.tree->root, "cons", nil, 0600, dctl) == nil)
		return nil;
	threadpostmountsrv(&srv, "rtdev", nil, 0);
	return dctl;
}
