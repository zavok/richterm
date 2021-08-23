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

void fs_open(Req *);
void fs_read(Req *);
void fs_write(Req *);
void ftree_destroy(File *);

Fsctl *
initfs(char *srvname)
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
	threadpostmountsrv(&srv, srvname, "/mnt/richterm", MREPL);
	return fsctl;
}

char *
ctlcmd(char *buf)
{
	Object **op;
	int n, i, j;
	char *args[256];
	n = tokenize(buf, args, 256);
	if (n <= 0) return "expected a command";
	qlock(rich.l);
	if (strcmp(args[0], "remove") == 0) {
		for (i = 1; i < n; i++) {
			for (j = 0; j < rich.objects->count; j++) {
				op = arrayget(rich.objects, j);
				if (*op == olast) continue;
				if (strcmp((*op)->id, args[i]) == 0) {
					objectfree(*op);
					arraydel(rich.objects, j);
					break;
				}
			}
		}
	} else if (strcmp(args[0], "clear") == 0) {
		int k;
		k = rich.objects->count;
		for (i = 0; i < k; i++) {
			op = arrayget(rich.objects, 0);
			if (*op != olast) {
				objectfree(*op);
				arraydel(rich.objects, 0);
			}
		}
	} else {
		qunlock(rich.l);
		return "unknown command";
	}
	qunlock(rich.l);
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
	newobj = mkobjectftree(newobject(&rich, nil, 0), fsctl->tree->root);
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
		aux->read(r, aux->data);
		respond(r, nil);
	} else respond(r, "fs_read: f->aux is nil");
}

void
fs_write(Req *r)
{
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
		aux->write(r, aux->data);
		respond(r, nil);
		redraw(1);
	} else respond(r, "fs_write: f->aux is nil");
}

void
arrayread(Req *r, void *v)
{
	Array *data;
	data = v;
	qlock(rich.l);
	qlock(data->l);
	readbuf(r, data->p, data->count);
	qunlock(data->l);
	qunlock(rich.l);
}

void
arraywrite(Req *, void *)
{
	/* stub */
}

void
textread(Req *r, void *)
{
	Faux *aux;
	Object *obj, *oe;
	char *s;
	usize n;

	qlock(rich.l);
	aux = r->fid->file->aux;
	obj = aux->obj;
	oe = obj->next;

	if (oe == nil) n = rich.text->count;
	else n = oe->offset;
	
	s = arrayget(rich.text, obj->offset);
	
	qlock(rich.text->l);

	readbuf(r, s, n - obj->offset);

	qunlock(rich.text->l);
	qunlock(rich.l);
}

void
textwrite(Req *r, void *)
{
	/* TODO: this is not exactly finished */
	/* in particular TRUNK/APPEND handling is needed */

	char *p, *pe;
	Faux *aux;
	Object *obj;
	long n, m, dn;

	qlock(rich.objects->l);

	aux = r->fid->file->aux;
	obj = aux->obj;
	p = rich.text->p + obj->offset;
	pe = rich.text->p + rich.text->count;
	if (obj->next == nil) n = rich.text->count;
	else n = obj->next->offset - obj->offset;
	m = r->ifcall.count + r->ifcall.offset;
	dn = m - n;

	if (dn > 0) arraygrow(rich.text, dn);
	else rich.text->count += dn;
	
	qlock(rich.text->l);

	memcpy(p + m, p + n,  pe - p);
	memcpy(p + r->ifcall.offset, r->ifcall.data,
	  r->ifcall.count);

	qunlock(rich.text->l);

	for (obj = obj->next; obj != nil; obj = obj->next) {
		obj->offset += dn;
	}

	qunlock(rich.objects->l);

}

void
fontread(Req *r, void *)
{
	Faux *aux;
	qlock(rich.l);
	aux = r->fid->file->aux;
	readstr(r, aux->obj->font->name);
	qunlock(rich.l);
}

void
fontwrite(Req *r, void *)
{
	char buf[4096], *bp;
	Faux *aux;
	qlock(rich.l);
	aux = r->fid->file->aux;
	memcpy(buf, r->ifcall.data, r->ifcall.count);
	buf[r->ifcall.count] = '\0';

	for(bp = buf+ r->ifcall.count - 1; bp >= buf; bp--)
		if ((*bp==' ')||(*bp=='\t')||(*bp=='\n')) *bp = '\0';

	for(bp = buf; bp < buf + r->ifcall.count - 1; bp++)
		if ((*bp!=' ')&&(*bp!='\t')&&(*bp!='\n')) break;

	aux->obj->font = getfont(fonts, bp);
	qunlock(rich.l);
}