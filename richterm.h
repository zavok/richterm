/*
 * Object is an atom of internal data structure.
 * There should be a list of Objects, manipulatable
 * either through GUI or file system.
 */
typedef struct Object Object;
struct Object {
	char *type;
	char *opts;
	char *data;
	long count;
};

int initdevfs(void);
