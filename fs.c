#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <draw.h>
#include <9p.h>

#include "array.h"
#include "richterm.h"

Array *consbuf, *ctlbuf, *menubuf;
Channel *consc, *ctlc;
File *fsroot;
File *cons, *consctl, *ctl, *menu, *new, *richtext;
Reqqueue *rq;

void arrayopen(Req *);
void arrayread(Req *);
void arraywrite(Req *);
void consread(Req *);
void conswrite(Req *);
void ctlread(Req *);
void ctlwrite(Req *);
void delayedread(Req *);
void fontread(Req *);
void fontwrite(Req *);
void fs_destroyfid(Fid *);
void fs_flush(Req *);
void fs_open(Req *);
void fs_read(Req *);
void fs_write(Req *);
void imageclose(Fid *);
void newopen(Req *);
void newread(Req *);
void textread(Req *);
void textwrite(Req *);

int
initfs(char *srvname)
{
	static Srv srv = {
		.open  = fs_open,
		.read  = fs_read,
		.write = fs_write,
		.flush = fs_flush,
		.destroyfid = fs_destroyfid,
	};
	consbuf = nil;
	rq = reqqueuecreate();
	menubuf = arraycreate(sizeof(char), 1024, nil);
	consc = chancreate(sizeof(Array *), 1024);

	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	fsroot = srv.tree->root;

	cons = createfile(fsroot, "cons", "richterm", DMAPPEND|0666,
		fauxalloc(nil, nil, consread, conswrite, nil));

	consctl = createfile(fsroot, "consctl", "richterm", DMAPPEND|0666, 
		fauxalloc(nil, nil, nil, nil, nil));

	richtext = createfile(fsroot, "richtext", "richterm", 0666,
		fauxalloc(richdata, nil, arrayread, arraywrite, nil));

	menu = createfile(fsroot, "menu", "richterm", 0666,
		fauxalloc(menubuf, nil, arrayread, arraywrite, nil));

	threadpostmountsrv(&srv, srvname, "/mnt/richterm", MREPL);
	return 0;
}

Faux *
fauxalloc(Array *data,
  void (*open)(Req *), void (*read)(Req *),
  void (*write)(Req *), void (*destroyfid)(Fid *))
{
	Faux *aux;
	aux = mallocz(sizeof(Faux), 1);
	*aux = (Faux) {data, open, read, write, destroyfid};
	return aux;
}

void
fs_destroyfid(Fid *fid)
{
}

void
fs_open(Req *r)
{
	Faux *aux;
	aux = r->fid->file->aux;
	if ((aux != nil) && (aux->open != nil)) aux->open(r);
	else respond(r, nil);
}

void
fs_read(Req *r)
{
	reqqueuepush(rq, r, delayedread);
}

void
fs_write(Req *r)
{
	Faux *aux;
	aux = r->fid->file->aux;
	if (aux != nil) {
		if (aux->write != nil) {
			aux->write(r);
			nbsend(redrawc, nil);
		}
		else respond(r, "no write");
	} else respond(r, "fs_write: f->aux is nil");
}

void
fs_flush(Req *r)
{
	respond(r, nil);
}

void
delayedread(Req *r)
{
	Faux *aux;
	aux = r->fid->file->aux;
	if (aux != nil) {
		if (aux->read != nil) aux->read(r);
		else respond(r, "no read");
	} else respond(r, "fs_read: f->aux is nil");
}

void
arrayopen(Req *r)
{
	
	respond(r, nil);
}

void
arrayread(Req *r)
{
	Array *data;
	data = ((Faux *)r->fid->file->aux)->data;
	qlock(rich.l);
	qlock(data->l);
	readbuf(r, data->p, data->count);
	qunlock(data->l);
	qunlock(rich.l);


	respond(r, nil);
}

void
arraywrite(Req *r)
{
	long count;
	Array *data;
	qlock(rich.l);
	data = ((Faux *)r->fid->file->aux)->data;
	count = r->ifcall.count + r->ifcall.offset;
	if (count > data->count) arraygrow(data, count - data->count, nil);
	else data->count = count;

//	data->count = 0;
//	arraygrow(data, r->ifcall.count, r->ifcall.data);
	qlock(data->l);
	memcpy(data->p + r->ifcall.offset, r->ifcall.data, r->ifcall.count);
	qunlock(data->l);
	r->ofcall.count = r->ifcall.count;
	r->fid->file->length = data->count;
	qunlock(rich.l);
	respond(r, nil);
}

void
consread(Req *r)
{
	if (consbuf == nil) recv(consc, &consbuf);
	r->ifcall.offset = 0;
	readbuf(r, consbuf->p, consbuf->count);
	if (arraydel(consbuf, 0, r->ofcall.count) != 0)
		sysfatal("consread: %r");
	if (consbuf->count == 0) {
		arrayfree(consbuf);
		consbuf = nil;
	}

	respond(r, nil);
}

void
conswrite(Req *r)
{
	Array *a;
	a = arraycreate(sizeof(char), r->ifcall.count, nil);
	arraygrow(a, r->ifcall.count, r->ifcall.data);
	nbsend(insertc, &a);
	r->ofcall.count = r->ifcall.count;

	respond(r, nil);
}

