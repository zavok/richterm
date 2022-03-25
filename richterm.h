typedef struct Rich Rich;
typedef struct Faux Faux;
typedef struct Token Token;
typedef struct Elem Elem;

struct Rich {
	QLock *l;
	Array *objects;
	u64int idcount;
	long selmin;
	long selmax;
	Rectangle r;
	Rectangle rs;
	int scroll;
	int max;
};

struct Faux {
	Array *data;
	void (*open)(Req *);
	void (*read)(Req *);
	void (*write)(Req *);
	void (*stat)(Req *);
	void (*destroyfid)(Fid *);
};

enum {
	E_NOOP  = '\0',
	E_TEXT  = '.',
	E_FONT  = 'f',
	E_LINK  = 'l',
	E_IMAGE = 'I',
	E_NL    = 'n',
	E_TAB   = 't',
	E_SPACE = 's',
};

enum {TRune, TFont, TLink, TImage};

struct Elem {
	char type;

	char *str;
	long count;

	// union {
		Rune r;
		char *link;
		Font *font;
		Image *image;
	// };

	Elem *next;
	Elem *prev;

	Point pos;
	Point nlpos;
};

extern Channel *redrawc;
extern Channel *insertc;
extern Channel *consc;
extern Channel *ctlc;
extern File *fsroot;
extern Array *menubuf;
extern Array *fonts;
extern Rich rich;
extern Array *elems;
extern Array *richdata;
extern Elem *euser;


Faux * fauxalloc(Array *, void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Fid *));
Font* getfont(Array *, char *);
Point drawelem(Elem *);
Point drawnl(Elem *);
Point drawnoop(Elem *);
Point drawspace(Elem *);
Point drawtab(Elem *);
// Point drawtext(Elem *);
Point drawrune(Elem *);
char * elemparse(Elem *, char *, long);
int initfs(char *);
void drawelems(void);
void drawpage(Image *, Rich *);
void drawscrollbar(void);
void elemslinklist(Array *);
void elemsupdatecache(Array *);
void freeelem(Elem *);
void generatesampleelems(void);
void parsedata(Array *, Array *);
