#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "richterm.h"

File *cons, *consctl;

void
devfs_read(Req *r)
{
	File *f;
	Devfsctl *dctl;
	int kv;
	f = r->fid->file;
	dctl = f->aux;
	if (f == cons) {
		recv(dctl->rc, &kv);
		r->ofcall.count = 1; //sizeof(int);
		memcpy(r->ofcall.data, &kv, 1); 
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
	Devfsctl *dctl;
	f = r->fid->file;
	dctl = f->aux;
	if (f == cons){
		char *buf;
		buf = mallocz(r->ifcall.count + 1, 1);
		/*
		 * + 1 is a hack to make sure string is \0 terminated
		 * we should send a struct that includes both data and size
		 * instead of simple char pointer.
		 */
		memcpy(buf, r->ifcall.data, r->ifcall.count);
		send(dctl->wc, &buf);
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
	dctl->rc = chancreate(sizeof(int), 1024);
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	if (srv.tree == nil) return nil;
	cons = createfile(srv.tree->root, "cons", "richterm", 0666, dctl);
	if (cons == nil) return nil;
	consctl = createfile(srv.tree->root, "consctl", "richterm", 0666, dctl);
	if (consctl == nil) return nil;
	threadpostmountsrv(&srv, nil, "/dev", MBEFORE);
	return dctl;
}
