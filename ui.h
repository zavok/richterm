/*
 * View is an atom of our main GUI screen.
 * It represents a small "window" through which
 * a part of object can be seen.
 * dp points to obj->data+x, so we can display
 * only a part of the object (used for wrapped
 * lines and such).
 * r holds data on where view fits on screen.
 * page holds pointer to page view belongs to.
 */
typedef struct View View;

/*
 * Page is a collection of views.
 */
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
};

void drawview(Image *, View *);
void drawpage(Image *, Page *);
Point viewsize(View *);