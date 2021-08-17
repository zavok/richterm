#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "array.h"
#include "richterm.h"

File *cons, *consctl;

void
devfs_read(Req *r)
{
	File *f;
	Devfsctl *dctl;
	Array *dv;
	f = r->fid->file;
	dctl = f->aux;
	if (f == cons) {
		recv(dctl->rc, &dv);
		//print("got: %s", dv->p);
		r->ofcall.count = dv->count;
		memcpy(r->ofcall.data, dv->p, dv->count);
		arrayfree(dv);
		respond(r, nil);
	} else if (f == consctl) {
		respond(r, "not implemented");
	}
	else respond(r, "what");
}

void
devfs_write(Req *r)
{
	File *f;
	f = r->fid->file;
	if (f == cons){
		char *buf;
		buf = mallocz(r->ifcall.count + 1, 1);
		memcpy(buf, r->ifcall.data, r->ifcall.count);
		mkobjectftree(newobject(&rich, buf), fsctl->tree->root);
		redraw(1);
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	} else if (f == consctl) {
		respond(r, "not implemented");
	} else respond(r, "what");
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
	
	dctl->wc = chancreate(sizeof(char *), 0);
	dctl->rc = chancreate(sizeof(Array *), 1024);
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	if (srv.tree == nil) return nil;
	cons = createfile(srv.tree->root, "cons", "richterm", 0666, dctl);
	if (cons == nil) return nil;
	consctl = createfile(srv.tree->root, "consctl", "richterm", 0666, dctl);
	if (consctl == nil) return nil;
	threadpostmountsrv(&srv, nil, "/dev", MBEFORE);
	return dctl;
}
