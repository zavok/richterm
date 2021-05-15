typedef struct Devfsctl Devfsctl;
struct Devfsctl {
	Channel *rc;
	Channel *wc;
};

typedef struct Object Object;
struct Object {
	/* old fields */
	char *type;
	char *opts;
	char *data;
	long count;
	/* future fields */
	char *text;
	char *font;
	char *link;
	char *image;
};

Devfsctl * initdevfs(void);
