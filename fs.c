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
File *cons, *consctl, *ctl, *menu, *new, *text;
Object *newobj;
Reqqueue *rq;


char * arrayread(Req *);
char * arraywrite(Req *);
char * consread(Req *);
char * conswrite(Req *);
char * ctlread(Req *);
char * ctlwrite(Req *);
char * fontread(Req *);
char * fontwrite(Req *);
char * newread(Req *);
char * textread(Req *);
char * textwrite(Req *);
void delayedread(Req *);
void fs_destroyfid(Fid *);
void fs_flush(Req *);
void fs_open(Req *);
void fs_read(Req *);
void fs_write(Req *);
void imageclose(Fid *);


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
	newobj = nil;
	consbuf = nil;
	rq = reqqueuecreate();
	menubuf = arraycreate(sizeof(char), 1024, nil);
	consc = chancreate(sizeof(Array *), 1024);
	ctlc = chancreate(sizeof(Array *), 1024);
	srv.tree = alloctree("richterm", "richterm", DMDIR|0555, nil);
	fsroot = srv.tree->root;
	new = createfile(fsroot, "new", "richterm", 0444, 
		fauxalloc(nil, nil, newread, nil));
	ctl = createfile(fsroot, "ctl", "richterm", 0666, 
		fauxalloc(nil, nil, ctlread, ctlwrite));
	text = createfile(fsroot, "text", "richterm", 0444, 
		fauxalloc(nil, rich.text, arrayread, nil));
	cons = createfile(fsroot, "cons", "richterm", 0666, 
		fauxalloc(nil, nil, consread, conswrite));
	consctl = createfile(fsroot, "consctl", "richterm", 0666, 
		fauxalloc(nil, nil, nil, nil));
	menu = createfile(fsroot, "menu", "richterm", 0666, 
		fauxalloc(nil, menubuf, arrayread, arraywrite));
	threadpostmountsrv(&srv, srvname, "/mnt/richterm", MREPL);
	return 0;
}

void
rmobjectftree(Object *obj)
{
	free(obj->ftext->aux);
	free(obj->ffont->aux);
	free(obj->flink->aux);
	free(obj->fimage->aux);

	removefile(obj->ftext);
	removefile(obj->ffont);
	removefile(obj->flink);
	removefile(obj->fimage);

	removefile(obj->dir);

	free(obj->id);
}

Object *
mkobjectftree(Object *obj, File *root)
{
	obj->id = smprint("%ulld", ++rich.idcount);

	obj->dir = createfile(root, obj->id, "richterm", DMDIR|0555, nil);

	obj->ftext  = createfile(obj->dir, "text",  "richterm", 0666,
	  fauxalloc(obj, nil, textread, textwrite));

	obj->ffont  = createfile(obj->dir, "font",  "richterm", 0666,
	  fauxalloc(obj, nil, fontread, fontwrite));

	obj->flink  = createfile(obj->dir, "link",  "richterm", 0666,
	  fauxalloc(obj, obj->dlink, arrayread, arraywrite));

	obj->fimage = createfile(obj->dir, "image", "richterm", 0666,
	  fauxalloc(obj, obj->dimage, arrayread, arraywrite));

	return obj;
}

char *
ctlcmd(char *buf)
{
	Object *obj;
	int n, i, j;
	char *args[256];
	obj = nil;
	n = tokenize(buf, args, 256);
	if (n <= 0) return "expected a command";
	qlock(rich.l);
	if (strcmp(args[0], "remove") == 0) {
		for (i = 1; i < n; i++) {
			for (j = 0; j < rich.objects->count; j++) {
				arrayget(rich.objects, j, &obj);
				if (obj == olast) continue;
				if (strcmp(obj->id, args[i]) == 0) {
					objsettext(obj, nil, 0);
					rmobjectftree(obj);
					objectfree(obj);
					free(obj);
					arraydel(rich.objects, j, 1);
					break;
				}
			}
		}
	} else if (strcmp(args[0], "clear") == 0) {
		for (i = 0; i < rich.objects->count; i++) {
			arrayget(rich.objects, i, &obj);
			if (obj != olast) rmobjectftree(obj);
			objectfree(obj);
			free(obj);
		}
		rich.objects->count = 0;
		rich.text->count = 0;
		olast = objectcreate();
		arraygrow(rich.objects, 1, &olast);
	} else {
		qunlock(rich.l);
		return "unknown command";
	}
	qunlock(rich.l);
	return nil;
}


void
fs_destroyfid(Fid *fid)
{
	if (fid->file == nil) return;
	if (fid->file->aux == nil) return;
	if (((Faux *)fid->file->aux)->obj == nil) return;
	if (((Faux *)fid->file->aux)->obj->fimage == nil) return;
	if (fid->file == ((Faux *)fid->file->aux)->obj->fimage) {
		imageclose(fid);
	}
}

void
fs_open(Req *r)
{
	if (r->fid->omode && OTRUNC) {
//		if ((r->fid->file->aux != nil) && (((Faux *)r->fid->file->aux)->data != nil))
//			((Faux *)r->fid->file->aux)->data->count = 0;
	}
	if (r->fid->file == new) {
		newobj = objectcreate();
		mkobjectftree(newobj, fsroot);
		objinsertbeforelast(newobj);
	}
	respond(r, nil);
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
	char *s;
	aux = r->fid->file->aux;
	if (aux != nil) {
		if (aux->write != nil) {
			s = aux->write(r);
			nbsend(redrawc, &aux->obj);
		}
		else s = "no write";
	} else s = "fs_write: f->aux is nil";
	respond(r, s);
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
	char *s;
	aux = r->fid->file->aux;
	if (aux != nil) {
		if (aux->read != nil) s = aux->read(r);
		else s = "no read";
	} else s = "fs_read: f->aux is nil";
	respond(r, s);	
}

char *
arrayread(Req *r)
{
	Array *data;
	data = ((Faux *)r->fid->file->aux)->data;
	qlock(rich.l);
	qlock(data->l);
	readbuf(r, data->p, data->count);
	qunlock(data->l);
	qunlock(rich.l);
	return nil;
}

char *
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
	return nil;
}

char *
textread(Req *r)
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
	
	s = arrayget(rich.text, obj->offset, nil);
	
	qlock(rich.text->l);

	readbuf(r, s, n - obj->offset);

	r->fid->file->length = objtextlen(obj);

	qunlock(rich.text->l);
	qunlock(rich.l);
	return nil;
}

char *
textwrite(Req *r)
{
	/* TODO: this is not exactly finished */
	/* in particular TRUNK/APPEND handling is needed */

	char *buf;
	Faux *aux;
	Object *obj;
	long n, m, k;

	aux = r->fid->file->aux;
	obj = aux->obj;

	n = r->ifcall.offset + r->ifcall.count;
	m = objtextlen(obj);
	k = (n > m) ? n : m;
	buf = malloc(sizeof(char) * k);
	
	qlock(rich.l);
	memcpy(buf, arrayget(rich.text, obj->offset, nil), m);
	memcpy(buf + r->ifcall.offset, r->ifcall.data, r->ifcall.count);
	objsettext(obj, buf, k);
	qunlock(rich.l);

	free(buf);
	r->ofcall.count = r->ifcall.count;
	return nil;
}

char *
fontread(Req *r)
{
	Faux *aux;
	qlock(rich.l);
	aux = r->fid->file->aux;
	readstr(r, aux->obj->font->name);
	qunlock(rich.l);
	return nil;
}

char *
fontwrite(Req *r)
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
	r->ofcall.count = r->ifcall.count;
	return nil;
}

char *
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
	return nil;
}

char *
conswrite(Req *r)
{
	Array *a;
	a = arraycreate(sizeof(char), r->ifcall.count, nil);
	arraygrow(a, r->ifcall.count, r->ifcall.data);
	nbsend(insertc, &a);
	r->ofcall.count = r->ifcall.count;
	return nil;
}

char *
ctlread(Req *r)
{
	if (ctlbuf == nil) recv(ctlc, &ctlbuf);
	r->ifcall.offset = 0;
	readbuf(r, ctlbuf->p, ctlbuf->count);
	if (arraydel(ctlbuf, 0, r->ofcall.count) != 0)
		sysfatal("ctlread: %r");
	if (ctlbuf->count == 0) {
		arrayfree(ctlbuf);
		ctlbuf = nil;
	}
	return nil;
}

char *
ctlwrite(Req *r)
{
	char *ret, *buf;
	buf = mallocz(r->ifcall.count + 1, 1);
	memcpy(buf, r->ifcall.data, r->ifcall.count);
	ret = ctlcmd(buf);
	free(buf);
	r->ofcall.count = r->ifcall.count;
	return ret;
}

char *
newread(Req *r)
{
	readstr(r, newobj->id);
	return nil;
}

void
imageclose(Fid *fid)
{
	Array *data;
	Rectangle r;
	char *p;
	int compressed, m;
	long n;
	ulong chan;
	Faux *aux;

	
	compressed = 0;
	aux = fid->file->aux;
	data = aux->data;
	
	if (aux->obj->image != nil) return;

	qlock(data->l);
	p = data->p;
	n = data->count;
	if (n < 10) {
		data->count = 0;
		aux->obj->ftext->length = 0;
		qunlock(data->l);
		fprint(2, "imageclose: image file too short\n");
		return;
	}
	if (strncmp("compressed\n", p, 11) == 0) {
		p += 11;
		n -= 11;
		compressed = 1;
	}
	if (n < 60) {
		data->count = 0;
		aux->obj->ftext->length = 0;
		qunlock(data->l);
		fprint(2, "imageclose: image file too short\n");
		return;
	}
	if ((chan = strtochan(p)) == 0) {
		data->count = 0;
		aux->obj->ftext->length = 0;
		qunlock(data->l);
		fprint(2, "imageclose: image channel unknown: %12s\n", p);
		return;
	}
	p += 12; n -= 12;
	r.min.x = atoi(p); p += 12; n -= 12;
	r.min.y = atoi(p); p += 12; n -= 12;
	r.max.x = atoi(p); p += 12; n -= 12;
	r.max.y = atoi(p); p += 12; n -= 12;

	lockdisplay(display);
	aux->obj->image = allocimage(display, r, chan, 0, DBlue);
	if (aux->obj->image == nil) {
		data->count = 0;
		aux->obj->ftext->length = 0;
		qunlock(data->l);
		unlockdisplay(display);
		fprint(2, "imageclose: allocimage failed: %r\n");
		return;
	}
	if (compressed != 0) m = cloadimage(aux->obj->image, r, (uchar *)p, n);
	else m = loadimage(aux->obj->image, r, (uchar *)p, n);
	if (m != n) {
		fprint(2, "imageclose: failed to load image\n");
		freeimage(aux->obj->image);
		data->count =  0;
		aux->obj->ftext->length = 0;
	}

	unlockdisplay(display);
	qunlock(data->l);

	return;
}
