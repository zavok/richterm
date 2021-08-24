typedef struct Array Array;

struct Array {
	QLock *l;
	long size;
	long n;
	long count;
	char *p;
	void (*free)(void *);
};

Array * arraycreate(long size, long n, void (*free)(void *));
int arraydel(Array *, long, long);
void arrayfree(Array *);
void * arraygrow(Array *, long, void *);
void * arrayget(Array *, long, void *);
void * arrayend(Array *);
void * arrayinsert(Array *, long, long, void *);
