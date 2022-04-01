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

void * 
_arrayget(Array *ap, long n)
{
	return (void *)(ap->p + ap->size * n);
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
			void **v;
			v = _arrayget(ap, i);
			if (*v != nil) ap->free(*v);
		}
	}
	qunlock(ap->l);
	free(ap);
}

void * 
arraygrow(Array *ap, long n, void *v)
{
	assert(ap->magic == MAGIC);

	char *ve;
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
	if (ap->count >= ap->n) {
		ap->n = 2 * ap->count;
		ap->p = realloc(ap->p, ap->size * ap->n);
	}

	ve = ap->p + ap->size * (ap->count - n);

	memset(ve, 0, n * ap->size);
	if (v != nil) {
		memcpy(ve, v, n * ap->size);
	}
	qunlock(ap->l);
	return (void *)ve;
}

int
arraydel(Array *ap, long offset, long count)
{
	assert(ap->magic == MAGIC);

	void *v, *ve;
	long i;
	if (_arraycheck(ap, offset, "arraydel") != 0) return -1;
	if (offset + count > ap->count) {
		werrstr("arraydel: count past array limit");
		return -1;
	};

	qlock(ap->l);

	if (ap->free != nil) {
		for (i = offset; i < offset+ count; i++) {
			v = _arrayget(ap, i);
			ap->free(v);
		}
	}

	v = _arrayget(ap, offset);
	ve = _arrayget(ap, offset + count);
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
	return _arrayget(ap, n);
}

void *
arrayend(Array *ap)
{
	assert(ap->magic == MAGIC);

	return _arrayget(ap, ap->count);
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

	qlock(ap->l);

	vs = _arrayget(ap, n);
	vn = _arrayget(ap, n + m);
	memcpy(vn, vs, (ap->count - n - m) * ap->size);
	memset(vs, 0, m * ap->size);
	if (v != nil) {
		memcpy(vs, v, m * ap->size);
	}
	qunlock(ap->l);
	return  vs;
}

void *
arrayset(Array *ap, long n, void *v)
{
	assert(ap->magic == MAGIC);
	if (_arraycheck(ap, n, "arrayset") != 0) return nil;
	qlock(ap->l);
	if (v != nil) {
		memcpy(ap->p + ap->size * n, v, ap->size);
	}
	qunlock(ap->l);
	return _arrayget(ap, n);
}