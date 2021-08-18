#include <u.h>
#include <libc.h>

#include "array.h"

Array * 
arraycreate(usize size, long n, void (*free)(void *))
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
	int i;
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
	if (n < 0) return nil;
	qlock(ap->l);
	ap->count += n;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		ap->p = realloc(ap->p, ap->size * ap->n);
	}
	memset(arrayget(ap, ap->count - n), 0, ap->size * n);
	qunlock(ap->l);
	return (void *)(ap->p + ap->size * (ap->count - n));
}

void
arraydel(Array *ap, long n)
{
	char *v;
	if ((n < 0) || (n > ap->count)) return;
	qlock(ap->l);
	v = ap->p + ap->size * n;
	if (ap->free != nil) ap->free(v);
	memcpy(v, v + ap->size, (ap->count - n) * ap->size);
	ap->count--;
	qunlock(ap->l);
}

void * 
arrayget(Array *ap, long n)
{
	if ((n < 0) || (n > ap->count)) return nil;
	return (void *)(ap->p + ap->size * n);
}
