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
	Faux *aux;
	f = r->fid->file;
	aux = f->aux;
	if (f == new) {
		respond(r, "not implemented");
	} else if (aux != nil) {
		readbuf(r, aux->data->p, aux->data->n);
		respond(r, nil);
	} else respond(r, "fs_read: f->aux is nil");
}

void
fs_write(Req *r)
{
	char *buf;
	long n, m;
	File *f;
	Faux *aux;
	f = r->fid->file;
	aux = f->aux;
	if (f == new) {
		respond(r, "not allowed");
	} else if (aux != nil) {
		/* TODO: this is not exactly finished */
		n = r->ifcall.offset + r->ifcall.count;
		m = (r->ifcall.offset > aux->data->n) ? aux->data->n : r->ifcall.offset;
		buf = mallocz(n, 1);
		memcpy(buf, aux->data->p, m);
		memcpy(buf + r->ifcall.offset, r->ifcall.data, r->ifcall.count);
		free(aux->data->p);
		aux->data->p = buf;
		aux->data->n = n;
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	} else respond(r, "fs_write: f->aux is nil");
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
	threadpostmountsrv(&srv, "richterm", "/mnt/richterm", MREPL);
	return fsctl;
}
