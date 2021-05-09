#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "richterm.h"

void
devfs_read(Req *r)
{
	Devfsctl *dctl;
	int kv;
	dctl = r->fid->file->aux;
	recv(dctl->rc, &kv);
	r->ofcall.count = 1; //sizeof(int);
	memcpy(r->ofcall.data, &kv, 1); 
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
	dctl->rc = chancreate(sizeof(int), 1024);
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	if (srv.tree == nil)
		return nil;
	if (createfile(srv.tree->root, "cons", "richterm", 0666, dctl) == nil)
		return nil;
	if (createfile(srv.tree->root, "consctl", "richterm", 0666, dctl) == nil)
		return nil;
	threadpostmountsrv(&srv, nil, "/dev", MBEFORE);
	return dctl;
}
