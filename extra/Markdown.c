#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>

char *data;
long count;

/* Lexer */

enum {
	TEOF = 0,
	TH0, TH1, TH2, TH3, TH4, TH5, TH6,
	TWORD, TWBRK, TPBRK,
	TUNDEF = -1,
};

typedef struct Token Token;
struct Token {
	int type;
	char c;
	String *s;
};

void (*lex)(void);
long p;
Token tok, *tokens;
int oldtype;

void lnewline(void);

void lheader(void);
void lhspace(void);
void lhword(void);

void lword(void);
void lspace(void);

char consume(void);
char peek(int);
void emit(void);

void emitwbrk(void);
void emitpbrk(void);

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

	tok.s = s_new();
	tok.type = TUNDEF;
	oldtype = TUNDEF;
	lex = lnewline;
	while(lex != nil) {
		lex();
	}
	tok.type = TEOF;
	emit();
}

void
lnewline(void)
{
	char c;
	c = peek(0);
	switch (c){
	case 0:
		lex = nil;
		break;
	case '\n':
		consume();
		emitpbrk();
		tok.type = TUNDEF;
		break;
	case '#':
		lex = lheader;
		consume();
		tok.type = TH0;
		break;
	default:
		lex = lword;
		emitwbrk();
		tok.type = TWORD;
	}
}

void
lword(void)
{
	char c;
	c = peek(0);
	switch (c) {
	case 0:
		lex = nil;
		emit();
		break;
	case '\n':
		lex = lnewline;
		consume();
		emit();
		tok.type = TUNDEF;
		break;
	case ' ':
		lex = lspace;
		consume();
		emit();
		emitwbrk();
		tok.type = TWORD;
		break;
	default:
		s_putc(tok.s, c);
		consume();
	}
}

void
lspace(void)
{
	char c;
	c = peek(0);
	switch (c) {
	case ' ':
	case '\n':
		lex = lheader;
		consume();
		break;
	default:
		lex = lword;
		tok.type = TWORD;
	}
}

void
lheader(void)
{
	char c;
	if ((tok.type >= TH0) && (tok.type < TH6)) tok.type++;
	else {
		/* an error */
		lex = nil;
		return;
	}
	c = peek(0);
	switch (c){
	case '#':
		consume();
		lex = lheader;
		break;
	case '\n':
		/* an error */
		lex = nil;
		break;
	case ' ':
		consume();
		lex = lhspace;
		break;
	default:
		/* an error */
		lex = nil;
	}
}

void
lhspace(void)
{
	char c;
	c = peek(0);
	switch(c) {
	case 0:
	case '\n':
		lex = nil;
	case ' ':
		consume();
		break;
	default:
		lex = lhword;
	}
}

void
lhword(void)
{
	char c;
	c = peek(0);
	switch(c) {
	case 0:
		lex = nil;
	case ' ':
		s_putc(tok.s, c);
		consume();
		lex = lhspace;
		break;
	case '\n':
		consume();
		emit();
		lex = lnewline;
		break;
	default:
		s_putc(tok.s, c);
		consume();
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
	s_terminate(tok.s);
	print("[%d] %s\n", tok.type, s_to_c(tok.s));
	
	/* TODO: should add token to tokens array */
	
	/* cleaning up tok state */
	s_reset(tok.s);
	oldtype = tok.type;
	tok.type = TUNDEF;
}

void
emitwbrk(void)
{
	if (oldtype == TWORD) {
		tok.type = TWBRK;
		emit();
	}
}

void
emitpbrk(void)
{
	if (oldtype != TPBRK) {
		tok.type = TPBRK;
		emit();
	}
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
