extern Channel *redrawc;
extern Channel *insertc;
extern Channel *consc;
extern File *fsroot;

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

	long offset;

	Object *next;
	Object *prev;

	Point startpt;
	Point endpt;
	Point nextlinept;
};

extern Object *olast;

void redraw(Object *);

Object * objectcreate(void);
Object * mkobjectftree(Object *, File *);
void objinsertbeforelast(Object *);
void rmobjectftree(Object *);
void objectfree(Object *);
long objtextlen(Object *obj);
void objsettext(Object *, char *, long);

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

int initfs(char *);

typedef struct Faux Faux;

struct Faux {
	Object *obj;
	Array *data;
	char * (*read)(Req *);
	char * (*write)(Req *);
};

Faux * fauxalloc(Object *, Array *, char * (*)(Req *), char * (*)(Req *));
