typedef struct Array Array;

struct Array {
	QLock *l;
	usize size;
	usize n;
	long count;
	char *p;
	void (*free)(void *);
};

Array * arraycreate(usize, long, void (*free)(void *));
void arrayfree(Array *);
void * arraygrow(Array *, long);
void arraydel(Array *, long);
void * arrayget(Array *, long);
