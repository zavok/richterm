#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "array.h"
#include "richterm.h"

File *new, *ctl;
Object *newobj;

char *
ctlcmd(char *buf)
{
	int n, i, j;
	char *args[256];
	n = tokenize(buf, args, 256);
	if (n <= 0) return "expected a command";
	if (strcmp(args[0], "remove") == 0) {
		for (i = 0; i < n; i++) {
			for (j = 0; j < rich.count; j++) {
				if (rich.obj[j] == olast) continue;
				if (strcmp(rich.obj[j]->id, args[i]) == 0) {
					Object **sp, **tp;
					rmobjectftree(rich.obj[j]);
					free(rich.obj[j]);
					sp = &rich.obj[j];
					tp = &rich.obj[j+1];
					memcpy(sp, tp, (rich.count - j - 1) * sizeof(Object *));
					rich.count--;
					break;
				}
			}
		}
	} else if (strcmp(args[0], "clear") == 0) {
		for (i = 0; i < rich.count; i++) {
			if (rich.obj[i] == olast) continue;
			rmobjectftree(rich.obj[i]);
			free(rich.obj[i]);
		}
		rich.count = 1;
		rich.obj = realloc(rich.obj, 1 * sizeof(Object *));
		rich.obj[0] = olast;
	} else return "unknown command";
	return nil;
}

void
ftree_destroy(File *f)
{
	Faux *aux;
	if (f->aux == nil) return;
	aux = f->aux;
	free(aux->data->p);
	free(aux->data);
	free(aux);
}

void
fs_open(Req *r)
{	
	Fsctl *fsctl;
	fsctl = new->aux;
	newobj = mkobjectftree(newobject(&rich, nil), fsctl->tree->root);
	respond(r, nil);
}

void
fs_read(Req *r)
{
	File *f;
	Faux *aux;
	f = r->fid->file;
	aux = f->aux;
	if (f == ctl) {
		respond(r, "not implemented");
	} else if (f == new) {
		if (newobj != nil)
			readstr(r, newobj->id);
		respond(r, nil);
	} else if (aux != nil) {
		qlock(rich.l);
		readbuf(r, aux->data->p, aux->data->n);
		qunlock(rich.l);
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
	if (f == ctl) {
		char *ret, *buf;
		buf = mallocz(r->ifcall.count + 1, 1);
		memcpy(buf, r->ifcall.data, r->ifcall.count);
		ret = ctlcmd(buf);
		free(buf);
		respond(r, ret);
	} else if (f == new) {
		respond(r, "not allowed");
	} else if (aux != nil) {
		qlock(rich.l);
		/* TODO: this is not exactly finished */
		n = r->ifcall.offset + r->ifcall.count;
		m = (r->ifcall.offset > aux->data->n) ? aux->data->n : r->ifcall.offset;
		buf = mallocz(n + 1, 1);
		memcpy(buf, aux->data->p, m);
		memcpy(buf + r->ifcall.offset, r->ifcall.data, r->ifcall.count);
		free(aux->data->p);
		aux->data->p = buf;
		aux->data->n = n;
		r->ofcall.count = r->ifcall.count;

		if (aux->type == FT_FONT) {
			char *path;
			tokenize(aux->data->p, &path, 1);
			aux->obj->font = getfont(fonts, path);
		}

		qunlock(rich.l);
		respond(r, nil);
		redraw(1);
	} else respond(r, "fs_write: f->aux is nil");
}

Fsctl *
initfs(void)
{
	Fsctl *fsctl;
	static Srv srv = {
		.open  = fs_open,
		.read  = fs_read,
		.write = fs_write,
	};
	newobj = nil;
	fsctl = mallocz(sizeof(Fsctl), 1);
	fsctl->c = chancreate(sizeof(int), 0);
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, ftree_destroy);
	if (srv.tree == nil) return nil;
	fsctl->tree = srv.tree;
	new = createfile(srv.tree->root, "new", "richterm", 0666, fsctl);
	if (new == nil) return nil;
	ctl = createfile(srv.tree->root, "ctl", "richterm", 0666, fsctl);
	if (ctl == nil) return nil;
	threadpostmountsrv(&srv, "richterm", "/mnt/richterm", MREPL);
	return fsctl;
}
