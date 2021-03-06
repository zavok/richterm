typedef struct DrawState DrawState;
typedef struct Elem Elem;
typedef struct Faux Faux;
typedef struct Rich Rich;
typedef struct Token Token;

struct DrawState {
	int n;
	Point pos;
	Point nlpos;
	char *link;
	Font *font;
};

struct Rich {
	QLock *l;
	Array *objects;
	u64int idcount;
	long selmin;
	long selmax;
	long input;
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

enum {TRune = '.', TFont = 'f', TLink = 'l', TImage = 'i'};

struct Elem {
	char type;

	union {
		Rune r;
		char *str;
		Image *image;
	};
};

extern Array *elems;
extern Array *fonts;
extern Array *menubuf;
extern Array *richdata;
extern Channel *consc;
extern Channel *ctlc;
extern Channel *insertc;
extern Channel *redrawc;
extern File *fsroot;
extern Rich rich;

Faux * fauxalloc(Array *, void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Req *), void (*)(Fid *));
Font* getfont(Array *, char *);
Point drawrune(DrawState *, Elem *);
Rectangle elemrect(DrawState *, Elem *);
Rune * getrunes(long, long);
char * getlink(long n);
int initfs(char *);
void clearelems(void);
void drawelems(long, long);
void drawpage(Image *, Rich *);
void drawscrollbar(void);
void freeelem(Elem *);
void parsedata(Array *, Array *);