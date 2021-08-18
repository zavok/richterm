#include <u.h>
#include <libc.h>

#include "array.h"

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
			void **v;
			v = arrayget(ap, i);
			ap->free(*v);
		}
	}
	qunlock(ap->l);
	free(ap);
}

void * 
arraygrow(Array *ap, long n)
{
	void *v;
	if (n < 0) {
		werrstr("arraygrow: negative growth size");
		return nil;
	}
	v = arrayend(ap);
	qlock(ap->l);
	ap->count += n;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		ap->p = realloc(ap->p, ap->size * ap->n);
	}
	memset(v, 0, ap->size * n);
	qunlock(ap->l);
	return (void *)(ap->p + ap->size * (ap->count - n));
}

int
arraydel(Array *ap, long n)
{
	char *v;
	if ((n < 0) || (n >= ap->count)) {
		werrstr("arraydel: out of bounds");
		return -1;
	}
	qlock(ap->l);
	v = ap->p + ap->size * n;
	if (ap->free != nil) ap->free(v);
	memcpy(v, v + ap->size, (ap->count - n) * ap->size);
	ap->count--;
	qunlock(ap->l);
	return 0;
}

void * 
arrayget(Array *ap, long n)
{
	if ((n < 0) || (n >= ap->count)) {
		werrstr("arrayget: out of bounds");
		return nil;
	}
	return (void *)(ap->p + ap->size * n);
}

void *
arrayend(Array *ap)
{
	return (void *)(ap->p + ap->size * ap->count);
}