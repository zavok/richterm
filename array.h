typedef struct Array Array;

struct Array {
	QLock *l;
	long size;
	long n;
	long count;
	char *p;
	void (*free)(void *);
};

Array * arraycreate(long, long, void (*free)(void *));
void arrayfree(Array *);
void * arraygrow(Array *, long);
void arraydel(Array *, long);
void * arrayget(Array *, long);
void * arrayend(Array *);