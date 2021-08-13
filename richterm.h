void redraw(int);
void drawscrollbar(void);

typedef struct Data Data;

struct Data {
	char *p;
	long n;
};

typedef struct Object Object;

struct Object {
	File *dir;
	File *ftext;
	File *ffont;
	File *flink;
	File *fimage;
	char *id;
	Data *dtext;
	Data *dfont;
	Data *dlink;
	Data *dimage;
	Font *font;
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
	Page *page;
	Object *obj;
	char *dp;
	long length;
	Image *color;
	Image *image;
	Rectangle r;
	int selected;
};

struct Page {
	Array *views;
	Point scroll;
	Point max;
	Rectangle r;
	Rectangle rs;
	usize selstart;
	usize selend;
};

void drawview(Image *, View *);
void drawpage(Image *, Page *);
Point viewsize(View *);

typedef struct Rich Rich;

struct Rich {
	QLock *l;
	Array *objects;
	u64int idcount;
	Page page;

};

extern Rich rich;

void generatepage(Rich *);
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
	Data *data;
};

enum {
	FT_TEXT,
	FT_FONT,
	FT_LINK,
	FT_IMAGE
};

Faux * fauxalloc(Object *, Data *, int);