typedef struct Data Data;

struct Data {
	char *p;
	long n;
};

Data strtodata(char *);

typedef struct Object Object;

struct Object {
	File *dir;
	File *ftext;
	File *ffont;
	File *flink;
	File *fimage;
	char *id;
};

typedef struct Fonts Fonts;

struct Fonts {
	Font **data;
	int size;
	int count;
};

Font* getfont(Fonts *, char *);
void addfont(Fonts *, Font *);

typedef struct View View;

typedef struct Page Page;

struct View {
	Object *obj;
	char *dp;
	long length;
	Font *font;
	Rectangle r;
	Page *page;
};

struct Page {
	View *view;
	long count;
	Point scroll;
	Point max;
	Rectangle r;
};

void drawview(Image *, View *);
void drawpage(Image *, Page *);
Point viewsize(View *);

typedef struct Rich Rich;

struct Rich {
	Object *obj;
	long count;
	Page page;
};

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

Fsctl * initfs(void);

typedef struct Faux Faux;

struct Faux {
	Data *data;
};

Faux * fauxalloc(char *);