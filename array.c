#include <u.h>
#include <libc.h>

#include "array.h"

Array * 
arraycreate(usize size, long n, void (*free)(void *))
{
	Array *ap;
	ap = mallocz(sizeof(Array), 1);
	ap->size = size;
	ap->count = 0;
	ap->n = n;
	ap->mem = mallocz(size * n, 1);
	ap->free = free;
	return ap;
}

void 
arrayfree(Array *ap)
{
	int i;
	if (ap->free != nil) {
		for (i = 0; i < ap->count; i ++) {
			void **v;
			v = arrayget(ap, i);
			ap->free(*v);
		}
	}
	free(ap);
}

void * 
arrayadd(Array *ap)
{
	ap->count++;
	if (ap->count > ap->n) {
		ap->n += ap->n;
		ap->mem = realloc(ap->mem, ap->size * ap->n);
	}
	return memset(arrayget(ap, ap->count - 1), 0, ap->size);
}

void
arraydel(Array *ap, long n)
{
	char *v;
	v = ap->mem + ap->size * n;
	if (ap->free != nil) ap->free(v);
	memcpy(v, v + ap->size, (ap->count - n) * ap->size);
	ap->count--;
}

void * 
arrayget(Array *ap, long n)
{
	return (void *)(ap->mem + ap->size * n);
}
