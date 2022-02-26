typedef struct Page Page;
typedef struct Rich Rich;
typedef struct Faux Faux;

struct Page {
	Point scroll;
	Point max;
	Rectangle r;
	Rectangle rs;
};

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

struct Faux {
	Array *data;
	void (*open)(Req *);
	void (*read)(Req *);
	void (*write)(Req *);
	void (*destroyfid)(Fid *);
};

Faux * fauxalloc(Array *, void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Fid *));
Font* getfont(Array *, char *);
int initfs(char *);
void drawpage(Image *, Rich *);
void drawscrollbar(void);
void generatepage(Rich *, long);

extern Channel *redrawc;
extern Channel *insertc;
extern Channel *consc;
extern File *fsroot;
extern Array *menubuf;
extern Array *fonts;
extern Rich rich;

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
Point drawtab(Elem *);
Point drawnoop(Elem *);
char * elemparse(Elem *, char *, long);
void elemslinklist(Array *);
void elemsupdatecache(Array *);
void freeelem(Elem *);
void generatesampleelems(void);
void parsedata(Array *, Array *);

extern Array *elems;
extern Array *richdata;
extern Elem *euser;
