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
	THEADER,
};

enum {
	SEOF = 0,
	SNEW,
	SDEFAULT,
	SHEADER,
	SSPACE,
};

typedef struct Token Token;
struct Token {
	int type;
	int header;
	char c;
};

int state;
long p;
Token tok, *tokens;

int lex(void);
int lnew(void);
int ldefault(void);
int lheader(void);
int lspace(void);
char consume(void);
char peek(int);
void emit(void);

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

	state = SNEW;
	while(state != SEOF) {
		state = lex();
	}
}

int
lex(void)
{
	switch(state) {
	case SNEW: return lnew();
	case SDEFAULT: return ldefault();
	case SHEADER: return lheader();
	case SSPACE: return lspace();
	}
	fprint(2, "lex err\n");
	return SEOF;
}

int
lnew(void)
{
	char c;
	c = peek(0);
	switch (c){
	case '#':
		tok.type = THEADER;
		return SHEADER;
	default:
		return SDEFAULT;
	}
}

int
ldefault(void)
{
	int newstate;
	tok.c = consume();
	switch (tok.c) {
	case 0:
		tok.type = TEOF;
		emit();
		newstate = SEOF;
		break;
	case '\n':
	case ' ':
		tok.type = TCHAR;
		tok.c = ' ';
		emit();
		newstate = SSPACE;
		break;
	default:
		tok.type = TCHAR;
		newstate = SDEFAULT;
		emit();
	}
	return newstate;
}

int
lheader(void)
{
	char c;
	c = peek(0);
	switch (c){
	case '#':
		tok.header++;
		consume();
		return SHEADER;
	case '\n':
		consume();
		emit();
		return SNEW;
	default:
		consume();
		return SHEADER;
	}
}

int
lspace(void)
{
	char c;
	c = peek(0);
	switch (c) {
	case ' ':
	case '\n':
		consume();
		return SSPACE;
	default:
		return SDEFAULT;
	}
}

char
consume(void)
{
	if (p < count) return data[p++];
	else return 0;
}

char
peek(int k)
{
	if (p + k < count) return data[p + k];
	else return 0;
}

void
emit(void)
{
	print("[%d %c]", tok.type, tok.c);
	/* TODO: should add tokens to tokens array */
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
