extern Channel *redrawc;
extern Channel *insertc;
extern Channel *consc;
extern Channel *ctlc;
extern File *fsroot;
extern Array *menubuf;

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
	int scroll;
	int max;
};

extern Rich rich;

void drawpage(Image *, Rich *);
void generatepage(Rich *, long);

int initfs(char *);

typedef struct Faux Faux;

struct Faux {
	Object *obj;
	Array *data;
	void (*open)(Req *);
	void (*read)(Req *);
	void (*write)(Req *);
	void (*destroyfid)(Fid *);
};

Faux * fauxalloc(Object *, Array *, void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Fid *));


/* **** New Code Beyond This Point **** */

enum {
	E_NOOP  = '\0',
	E_TEXT  = '.',
	E_FONT  = 'F',
	E_LINK  = 'L',
	E_IMAGE = 'I',
	E_NL    = 'n',
	E_TAB   = 't',
	E_SPACE = 's'
};

typedef struct Token Token;
typedef struct Elem Elem;

struct Token {
	int type;
	char *str;
	long count;
};

struct Elem {
	Token;

	Elem *next;
	Elem *prev;

	char *link;
	Image *image;
	Font *font;

	Point pos;
	Point nlpos;
};

void drawelems(void);
Point drawelem(Elem *);
Point drawtext(Elem *);
Point drawnl(Elem *);
Point drawspace(Elem *);
Point drawnoop(Elem *);
char * elemparse(Elem *, char *, long);
void elemslinklist(Array *);
void elemsupdatecache(Array *);
void generatesampleelems(void);
void parsedata(Array *, Array *);

extern Array *elems;
extern Array *richdata;
extern Elem *euser;
