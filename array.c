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
arrayadd(Array *ap)
{
	qlock(ap->l);
	ap->count++;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		ap->mem = realloc(ap->mem, ap->size * ap->n);
	}
	memset(arrayget(ap, ap->count - 1), 0, ap->size);
	qunlock(ap->l);
	return (void *)(ap->mem + ap->size * (ap->count - 1));
}

void
arraydel(Array *ap, long n)
{
	char *v;
	qlock(ap->l);
	v = ap->mem + ap->size * n;
	if (ap->free != nil) ap->free(v);
	memcpy(v, v + ap->size, (ap->count - n) * ap->size);
	ap->count--;
	qunlock(ap->l);
}

void * 
arrayget(Array *ap, long n)
{
	return (void *)(ap->mem + ap->size * n);
}
