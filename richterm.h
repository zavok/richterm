void redraw(int);
void drawscrollbar(void);

typedef struct Object Object;

struct Object {
	File *dir;
	File *ftext;
	File *ffont;
	File *flink;
	File *fimage;
	char *id;
	Array *dlink;
	Array *dimage;
	Font *font;
	Image *image;

	Array *text;
	long offset;
	Object *next;
	Object *prev;
	Point startpt;
	Point endpt;
	Point nextlinept;
};

extern Object *olast;

Object * mkobjectftree(Object *, File *);
void rmobjectftree(Object *);
void objectfree(void *);

extern Array *fonts;

Font* getfont(Array *, char *);

typedef struct Page Page;

struct Page {
	Point scroll;
	Point max;
	Rectangle r;
	Rectangle rs;
};

typedef struct Rich Rich;

struct Rich {
	QLock *l;
	Array *objects;
	Array *text;
	u64int idcount;
	long selmin;
	long selmax;
	Page page;
};

extern Rich rich;

void drawpage(Image *, Rich *);
void generatepage(Rich *, long);
Object * newobject(Rich *, char *, long);

typedef struct Devfsctl Devfsctl;

struct Devfsctl {
	Channel *rc;
	Channel *wc;
};

Devfsctl * initdevfs(void);

typedef struct Fsctl Fsctl;

struct Fsctl {
	Channel *c;
	Tree *tree;
};

extern Fsctl *fsctl;

Fsctl * initfs(char *);

typedef struct Faux Faux;

struct Faux {
	int type;
	Object *obj;
	Array *data;
	void (*read)(Req *, void *);
	void (*write)(Req *, void *);
};

enum {
	FT_TEXT,
	FT_FONT,
	FT_LINK,
	FT_IMAGE
};

Faux * fauxalloc(Object *, Array *, int);

void textread(Req *, void *);
void textwrite(Req *, void *);
void arrayread(Req *, void *);
void arraywrite(Req *, void *);
void fontread(Req *, void *);
void fontwrite(Req *, void *);
