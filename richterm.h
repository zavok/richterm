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

Object * mkobjectftree(Object *, File *);
void rmobjectftree(Object *);

typedef struct Fonts Fonts;

struct Fonts {
	Font **data;
	int size;
	int count;
};

extern Fonts fonts;

Font* getfont(Fonts *, char *);
void addfont(Fonts *, Font *);

typedef struct View View;

typedef struct Page Page;

struct View {
	Page *page;
	Object *obj;
	char *dp;
	long length;
	Image *color;
	Rectangle r;
};

struct Page {
	View *view;
	long count;
	Point scroll;
	Point max;
	Rectangle r;
	Rectangle rs;
};

void drawview(Image *, View *);
void drawpage(Image *, Page *);
Point viewsize(View *);

typedef struct Rich Rich;

struct Rich {
	QLock *l;
	Object **obj;
	usize count;
	usize idcount;
	Page page;
	usize selstart;
	usize selend;
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