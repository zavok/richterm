#include <u.h>
#include <libc.h>

#include "array.h"

int
_arraycheck(Array *ap, long n, char *s)
{
	if ((n < 0) || (n >= ap->count)) {
		werrstr("%s: out of bounds", s);
		return -1;
	}
	return 0;
}

Array * 
arraycreate(long size, long n, void (*free)(void *))
{
	Array *ap;
	ap = mallocz(sizeof(Array), 1);
	*ap = (Array) {
		mallocz(sizeof(QLock), 1),
		size,
		n,
		0,
		mallocz(size * n, 1),
		free,
	};
	return ap;
}

void 
arrayfree(Array *ap)
{
	long i;
	qlock(ap->l);
	if (ap->free != nil) {
		for (i = 0; i < ap->count; i ++) {
			void *v;
			v = nil;
			arrayget(ap, i, &v);
			if (v != nil) ap->free(v);
		}
	}
	qunlock(ap->l);
	free(ap);
}

void * 
arraygrow(Array *ap, long n, void *v)
{
	void *ve;
	if (n < 0) {
		werrstr("arraygrow: negative growth size");
		return nil;
	}
	ve = arrayend(ap);
	qlock(ap->l);
	ap->count += n;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		if (ap->count > ap->n) ap->n = ap->count;
		ap->p = realloc(ap->p, ap->size * ap->n);
	}
	memset(ve, 0, ap->size * ap->n);
	if (v != nil) {
		memcpy(ve, v, n * ap->size);
	}
	qunlock(ap->l);
	return (void *)(ap->p + ap->size * (ap->count - n));
}

int
arraydel(Array *ap, long n)
{
	char *v;
	if (_arraycheck(ap, n, "arraydel") != 0) return -1;
	qlock(ap->l);
	v = ap->p + ap->size * n;
	if (ap->free != nil) ap->free(v);
	memcpy(v, v + ap->size, (ap->count - n) * ap->size);
	ap->count--;
	qunlock(ap->l);
	return 0;
}

void * 
arrayget(Array *ap, long n, void *v)
{
	if (_arraycheck(ap, n, "arrayget") != 0) return nil;
	qlock(ap->l);
	if (v != nil) {
		memcpy(v, ap->p + ap->size * n, ap->size);
	}
	qunlock(ap->l);
	return (void *)(ap->p + ap->size * n);
}

void *
arrayend(Array *ap)
{
	return (void *)(ap->p + ap->size * ap->count);
}

void *
arrayinsert(Array *ap, long n, long m, void *v)
{
	void *vs, *vn;
	if (n == ap->count) {
		vs = arraygrow(ap, m, v);
		return vs;
	}
	if (_arraycheck(ap, n, "arrayinsert") != 0) return nil;
	arraygrow(ap, m, nil);
	vs = arrayget(ap, n, nil);
	vn = arrayget(ap, n + m, nil);
	qlock(ap->l);
	memcpy(vn, vs, (ap->count - n - m) * ap->size);
	memset(vs, 0, m * ap->size);
	if (v != nil) {
		memcpy(vs, v, m * ap->size);
	}
	qunlock(ap->l);
	return  vs;
}
