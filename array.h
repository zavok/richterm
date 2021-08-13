typedef struct Array Array;

struct Array {
	QLock *l;
	usize size;
	usize n;
	long count;
	char *mem;
	void (*free)(void *);
};

Array * arraycreate(usize, long, void (*free)(void *));
void arrayfree(Array *);
void * arrayadd(Array *);
void arraydel(Array *, long);
void * arrayget(Array *, long);
