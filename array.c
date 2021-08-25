#include <u.h>
#include <libc.h>

#include "array.h"

#define MAGIC 0x1234

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
		MAGIC,
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
	assert(ap->magic == MAGIC);

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
	assert(ap->magic == MAGIC);

	void *ve;
	if (n < 0) {
		werrstr("arraygrow: negative growth size");
		return nil;
	}
	if (n == 0) {
		werrstr("arraygrow: zero growth size");
		return nil;
	}
	qlock(ap->l);

	ap->count += n;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		if (ap->count > ap->n)
			ap->n = ap->count;
		ap->p = realloc(ap->p, ap->size * ap->n);
	}

	ve = (void *)(ap->p + ap->size * (ap->count - n));

	memset(ve, 0, n * ap->size);
	if (v != nil) {
		memcpy(ve, v, n * ap->size);
	}
	qunlock(ap->l);
	return ve;
}

int
arraydel(Array *ap, long offset, long count)
{
	assert(ap->magic == MAGIC);

	void *v, *ve;
	long i;
	if (_arraycheck(ap, offset + count, "arraydel") != 0) return -1;

	if (ap->free != nil) {
		for (i = offset; i < offset+ count; i++) {
			v = arrayget(ap, i, nil);
			ap->free(v);
		}
	}

	v = arrayget(ap, offset, nil);
	ve = arrayget(ap, offset + count, nil);
	qlock(ap->l);
	memcpy(v, ve, (ap->count - offset - count) * ap->size);
	ap->count -= count;
	qunlock(ap->l);
	return 0;
}

void * 
arrayget(Array *ap, long n, void *v)
{
	assert(ap->magic == MAGIC);

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
	assert(ap->magic == MAGIC);

	return (void *)(ap->p + ap->size * ap->count);
}

void *
arrayinsert(Array *ap, long n, long m, void *v)
{
	assert(ap->magic == MAGIC);

	void *vs, *vn;
	if (n == ap->count) {
		vs = arraygrow(ap, m, v);
		return vs;
	}
	if (_arraycheck(ap, n, "arrayinsert") != 0) return nil;
	if (arraygrow(ap, m, nil) == nil) {
		werrstr("arrayinsert: %r");
		return nil;
	}
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
