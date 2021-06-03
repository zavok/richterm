typedef struct Data Data;

struct Data {
	char *p;
	long size;
};

typedef struct Object Object;

struct Object {
	/* old fields */
	char *type;
	char *opts;
	char *data;
	long count;
	/* future fields */
	Data text;
	Data font;
	Data link;
	Data image;
};

Data strtodata(char *);


typedef struct Devfsctl Devfsctl;

struct Devfsctl {
	Channel *rc;
	Channel *wc;
};

Devfsctl * initdevfs(void);


typedef struct Fsctl Fsctl;

struct Fsctl {
	Channel *c;
};

Fsctl * initfs(void);
