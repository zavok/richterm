typedef struct Obj Obj;
struct Obj {
	char *type;
	char *opts;
	char *data;
	long count;
};

typedef struct View View;
struct View {
	Obj *obj;
	long count;
};

void usage(void);
int initview(View *);
int initdevfs(void);
