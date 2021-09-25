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
	TLINK,
	TUNDEF = -1,
};

typedef struct Token Token;
struct Token {
	int type;
	char c;
	String *s;
};

void (*lex)(void);
long p, tokn;
Token tok, *tokens;
int oldtype;

void lnewline(void);

void lheader(void);
void lhspace(void);
void lhword(void);
void lhline(void);
void lsubhline(void);

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

void printtoken(Token);

char *root = "/mnt/richterm";

void
main(int argc, char **argv)
{
	int fd;
	char buf[1024];
	long i, n;

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

	tokens = nil;
	tokn = 0;
	tok.s = s_new();
	tok.type = TUNDEF;
	oldtype = TUNDEF;
	lex = lnewline;
	while(lex != nil) {
		lex();
	}
	emitpbrk();
	tok.type = TEOF;
	emit();

	for (i = 0; i < tokn; i++) {
		if (tokens[i].type == TEOF) break;
		printtoken(tokens[i]);
	}
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
	case '=':
		lex = lhline;
		consume();
		break;
	case '-':
		lex =lsubhline;
		consume();
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
		s_putc(tok.s, c);
		consume();
		//emit();
		//emitwbrk();
		//tok.type = TWORD;
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
		consume();
		break;
	case '\n':
		tok.type = TWORD;
		lex = lnewline;
		consume();
		emit();
		tok.type = TUNDEF;
		break;
	default:
		lex = lword;
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
		break;
	case ' ':
		s_putc(tok.s, c);
		consume();
		lex = lhspace;
		break;
	case '\n':
		consume();
		emit();
		emitpbrk();
		lex = lnewline;
		break;
	default:
		s_putc(tok.s, c);
		consume();
	}
}

void
lhline(void)
{
	char c;
	c = peek(0);
	switch (c) {
	case 0:
		lex = nil;
		break;
	case '=':
		consume();
		s_putc(tok.s, c);
		break;
	case '\n':
		lex = lnewline;
		consume();
		tokens[tokn - 1].type = TH1;
		break;
	default:
		lex = lword;
		consume();
		tok.type = TWORD;
		emit();
	};
}

void
lsubhline(void)
{
	char c;
	c = peek(0);
	switch (c){
	case 0:
		lex = nil;
		break;
	case '-':
		consume();
		s_putc(tok.s, c);
		break;
	case '\n':
		lex = lnewline;
		tokens[tokn - 1].type = TH2;
		consume();
		break;
	default:
		lex = lword;
		tok.type = TWORD;
		consume();
		emit();
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

	tokens = realloc(tokens, (tokn + 1) * sizeof(Token));
	tokens[tokn] = tok;
	tokn++;

	tok.s = s_new();
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

void
printtoken(Token tok)
{
	char *font, *text, *id;
	font = nil;
	text = s_to_c(tok.s);
	switch(tok.type) {
	case TH1:
		font = "/lib/font/bit/lucida/unicode.32.font";
		break;
	case TH2:
		font = "/lib/font/bit/lucida/unicode.28.font";
		break;
	case TWORD:
		break;
	case TWBRK:
		text = " ";
		break;
	case TPBRK:
		text = "\n\n";
		break;
	}
	id = newobj();
	if (text != nil) setfield(id, "text", text);
	if (font != nil) setfield(id, "font", font);
}
