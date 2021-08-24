#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "array.h"
#include "richterm.h"

File *cons, *consctl, *text;

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
		r->ofcall.count = dv->count;
		memcpy(r->ofcall.data, dv->p, dv->count);
		arrayfree(dv);
		respond(r, nil);
	} else if (f == consctl) {
		respond(r, "not implemented");
	}else if (f == text) {
		arrayread(r, rich.text);
		respond(r, nil);
	} else respond(r, "what");
}

void
devfs_write(Req *r)
{
	File *f;
	f = r->fid->file;
	if (f == cons){
		Object *obj;
		r->ofcall.count = r->ifcall.count;

		obj = objectcreate();
		mkobjectftree(obj, fsctl->tree->root);
		objinsertbeforelast(obj);

//print("%ld -> ", olast->offset);

		arrayinsert(rich.text, olast->offset, r->ifcall.count, r->ifcall.data);
		olast->offset += r->ifcall.count;

//print("%ld\n", olast->offset);


		nbsend(redrawc, &obj);
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
	text = createfile(srv.tree->root, "text", "richterm", 0444, dctl);
	threadpostmountsrv(&srv, nil, "/dev", MBEFORE);
	return dctl;
}
