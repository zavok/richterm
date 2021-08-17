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
	Array *dtext;
	Array *dfont;
	Array *dlink;
	Array *dimage;
	Font *font;
	Image *image;

	Array *text;
	usize offset;
};

extern Object *olast;

Object * mkobjectftree(Object *, File *);
void rmobjectftree(Object *);
void objectfree(void *);

extern Array *fonts;

Font* getfont(Array *, char *);

typedef struct View View;

typedef struct Page Page;

struct View {
	Object *obj;
	char *dp;
	long length;
	Image *color;
	Rectangle r;
};

struct Page {
	Point scroll;
	Point max;
	Rectangle r;
	Rectangle rs;
};

void drawview(Image *, View *);
Point viewsize(View *);

typedef struct Rich Rich;

struct Rich {
	QLock *l;
	Array *objects;
	Array *views;
	Array *text;
	u64int idcount;
	Page page;
	struct {
		View *v[2];
		long n[2];
	} sel;
};

extern Rich rich;

void drawpage(Image *, Rich *);
void generatepage(Rich *, long);
Object * newobject(Rich *, char *);

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

Fsctl * initfs(void);

typedef struct Faux Faux;

struct Faux {
	int type;
	Object *obj;
	Array *data;
	void (*read)(Req *);
	void (*write)(Req *);
};

enum {
	FT_TEXT,
	FT_FONT,
	FT_LINK,
	FT_IMAGE
};

Faux * fauxalloc(Object *, Array *, int);

void textread(Req *);
void textwrite(Req *);
void arrayread(Req *);
void arraywrite(Req *);
