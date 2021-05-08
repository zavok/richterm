typedef struct Devfsctl Devfsctl;
struct Devfsctl {
	Channel *rc;
	Channel *wc;
};

typedef struct Object Object;
struct Object {
	char *type;
	char *opts;
	char *data;
	long count;
};

Devfsctl * initdevfs(void);
