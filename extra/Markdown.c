#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>

char *data;
long count;

/* Lexer */

enum {
	TEOF = 0,
	TCHAR,
};

enum {
	SEOF = 0,
	SDEFAULT,
};

typedef struct Token Token;
struct Token {
	int type;
	char c;
};

int state;
long p;
Token *tokens;

char consume(void);
char peek(void);

/* Rich */

char * newobj(void);
int setfield(char *, char *, char *);

char *root = "/mnt/richterm";

void
main(int argc, char **argv)
{
	int fd;
	char buf[1024];
	long n;

	if (argc > 1) {
		if ((fd = open(argv[1], OREAD)) < 0)
			sysfatal("can't open %s, %r", argv[1]);
	} else fd = 0;
	count = 0;
	data = nil;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		data = realloc(data, count + n);
		memcpy(data + count, buf, n);
		count += n;
	}
	if (n < 0) sysfatal("%r");
}

int
setfield(char *id, char *field, char *value)
{
	char *path;
	int fd;
	usize n;
	path = smprint("%s/%s/%s", root, id, field);
	fd = open(path, OWRITE);
	if (fd < 0) sysfatal("%r");
	n = write(fd, value, strlen(value));
	if (n != strlen(value)) sysfatal("write failed: %r");
	close(fd);
	free(path);
	return 0;
}

char *
newobj(void)
{
	char *buf;
	int fd;
	fd = open("/mnt/richterm/new", OREAD);
	if (fd < 0) sysfatal("%r");
	buf = mallocz(256, 1);
	read(fd, buf, 256);
	close(fd);
	return buf;
}
