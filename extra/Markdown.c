#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>

#include "config.h"

#define ATTACH(array, size, new) \
	{ \
		array = realloc(array, sizeof(*array) * (size + 1)); \
		array[size] = new; \
		size++; \
	}

#define APPEND(t1, t2) \
	{ \
		ATTACH(t1->tokens, t1->count, t2) \
		t2 = t1; \
		t1 = nil; \
	}

typedef struct Token Token;

struct Token {
	int type;
	union {
		Rune rune;
		struct {
			int count;
			Token **tokens;
		};
	};
};

enum {
	TNil,

	TRune,

	TSpace,
	TNewline,
	TTab,
	TBraceOpen,
	TBraceClose,
	TSqrBraceOpen,
	TSqrBraceClose,
	THash,
	TQuote,

	TWhiteSpace,
	THMarker,
	TWord,
	TWords,
	TQuoted,
	TBraced,
	TSqrBraced,
	TLink,
	TText,
	THeader,
	TLine,
	TEmptyLine,
	TParagraph,

	TMax,
};

char *names[] = {
	[TNil] "nil",
	[TRune] "rune",

	[TSpace] "sp",
	[TNewline] "nl",
	[TTab] "tab",
	[TBraceOpen] "(",
	[TBraceClose] ")",
	[TSqrBraceOpen] "[",
	[TSqrBraceClose] "]",
	[THash] "#",
	[TQuote] "\"",

	[TWhiteSpace] "ws",
	[THMarker] "h_marker",
	[TWord] "word",
	[TWords] "words",
	[TQuoted] "quoted",
	[TBraced] "()",
	[TSqrBraced] "[]",
	[TLink] "link",
	[TText] "text",
	[THeader] "h",
	[TLine] "line",
	[TEmptyLine] "pb",
	[TParagraph] "p",
};

Biobuf *bfdin;

Token* twrap(int, int, Token **);
Token** token1(Token *);

void input(void *);
void header(void *);
void pass1(void *);
void quote(void *);
void pass2(void *);
void words(void *);
void pass3(void *);
void link(void *);
void line(void *);
void debug(void *);
void output(void *);
void clear(void *);

void freetoken(Token *t);
void dbgprinttoken(Biobuf *, Token *, int);
void printtoken(Biobuf *, Token *);
void printlink(Biobuf *, Token *);
char * tokentotext(Token *, int);

Rune trune(Token *);
int ttype(Token *);
Token ** findtype(Token **, int, int);

void
usage(void)
{
	fprint(2, "usage: %s [file]", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	ARGBEGIN {
	default:
		usage();
	}ARGEND

	if (argc > 0) {
		bfdin = Bopen(argv[0], OREAD);
		if (bfdin == nil) sysfatal("%r");
	} else bfdin = Bfdopen(0, OREAD);

	int n;
	Channel **c;
	void (*pipeline[])(void *) = {
		input,
		pass1,
		quote,
		pass2,
		words,
		pass3,
		link,
//		header,
		line,
//		debug,
		output,
		clear,
	};

	n = sizeof(pipeline) / sizeof(*pipeline);

	c = mallocz(sizeof(Channel *) * (n + 1), 1);

	int i;
	for (i = 1; i < n; i ++) c[i] = chancreate(sizeof(Token *), 64);
	for (i = 0; i < n; i++) threadcreate(pipeline[i], (void *)(c + i), 64 * 1024);
}

Token *
twrap(int type, int count, Token **tokens)
{
	Token *nt = mallocz(sizeof(Token), 1);
	nt->type = type;
	nt->count = count;
	nt->tokens = tokens;
	return nt;
}

Token **
token1(Token *t)
{
	Token **tt;
	tt = malloc(sizeof(Token *));
	tt[0] = t;
	return tt;
}

void
input(void *v)
{
	Channel **c = v;

	Rune r;
	while ((r = Bgetrune(bfdin)) != Beof) {
		Token *t = mallocz(sizeof(Token), 1);
		t->type = TRune;
		t->rune = r;
		send(c[1], &t);
	}
	chanclose(c[1]);
}

void
header(void *v)
{
	Channel **c = v;
	Token *t, *nt, **tt;
	tt = nil;
	int h = 0;
	int count = 0;
	while (recv(c[0], &t) > 0) {
		if (h == 0) {
			if (t->type == THMarker) h = 1;
			else send(c[1], &t);
		}
		if (h != 0) {
			
			if ((t->type == TNewline) || (t->type == TEmptyLine))  {
				h = 0;
				nt = twrap(THeader, count, tt);
				send(c[1], &nt);
				send(c[1], &t);
				tt = nil;
				count = 0;
			} else ATTACH(tt, count, t)
		}
	}
	if (tt != nil) {
		nt = twrap(THeader, count, tt);
		send(c[1], &nt);
	}
	chanclose(c[1]);
}

void
pass1(void *v)
{
	Channel **c = v;
	Token *t;
	while (recv(c[0], &t) > 0) {
		if (ttype(t) == TRune) {
			switch (trune(t)) {
			case L'[':
				t = twrap(TSqrBraceOpen, 1, token1(t));
				break;
			case L']':
				t = twrap(TSqrBraceClose, 1, token1(t));
				break;
			case L'(':
				t = twrap(TBraceOpen, 1, token1(t));
				break;
			case L')':
				t = twrap(TBraceClose, 1, token1(t));
				break;
			case L'\n':
				t = twrap(TNewline, 1, token1(t));
				break;
			case L' ':
				t = twrap(TSpace, 1, token1(t));
				break;
			case L'\t':
				t = twrap(TTab, 1, token1(t));
				break;
			case L'\#':
				t = twrap(THash, 1, token1(t));
				break;
			case L'\"':
				t = twrap(TQuote, 1, token1(t));
				break;
			}
			send(c[1], &t);
		}
	}
	chanclose(c[1]);
}

void
quote(void *v)
{
	Channel **c = v;
	Token *t, *q = nil;
	while (recv(c[0], &t) > 0) {
		if (q == nil) {
			if (ttype(t) == TQuote) {
				q = twrap(TQuoted, 1, token1(t));
			} else send(c[1], &t);
		} else {
			if (ttype(t) == TQuote) {
				ATTACH(q->tokens, q->count, t)
				send(c[1], &q);
				q = nil;
			} else ATTACH(q->tokens, q->count, t)
		}
	}
	if (q != nil) {
		fprint(2, "missing end quote\n");
		send(c[1], &q);
	}
	chanclose(c[1]);
}

void
pass2(void *v)
{
	Channel **c = v;
	Token *t[2] = {nil, nil};
	while (recv(c[0], &t[0]) > 0) {
		switch(ttype(t[1])) {
		case TTab:
		case TSpace:
			t[1] = twrap(TWhiteSpace, 1, token1(t[1]));
			break;
		case THash:
			t[1] = twrap(THMarker, 1, token1(t[1]));
			break;
		case TRune:
			t[1] = twrap(TWord, 1, token1(t[1]));
			break;
		}

		switch(ttype(t[1])) {
		case TNewline:
			if (ttype(t[0]) == TNewline) {
				t[1] = twrap(TEmptyLine, 1, token1(t[1]));
				APPEND(t[1], t[0])
			}
			break;
		case TWhiteSpace:
			if ((ttype(t[0]) == TSpace) || (ttype(t[0]) == TTab)) {
				APPEND(t[1], t[0])
			}
			break;
		case THMarker:
			if (ttype(t[0]) == THash) {
				APPEND(t[1], t[0])
			}
			break;
		case TWord:
			if (ttype(t[0]) == TRune) {
				APPEND(t[1], t[0])
			}
			break;
		}

		if (t[1] != nil) send(c[1], &t[1]);
		t[1] = t[0];
		t[0] = nil;
	}
	if (t[1] != nil) send(c[1], &t[1]);
	chanclose(c[1]);
}

void
words(void *v)
{
	Channel **c = v;
	Token *t, **buf;
	char bf = 0xff;
	buf = mallocz(sizeof(Token *) * 8, 1);
	int r = 1;
	while (bf != 0) {
		t = nil;
		if (r > 0) {
			recv(c[0], &t);
		}
		memcpy(buf, buf + 1, 7 * sizeof(Token *));
		buf[7] = t;
		bf = (bf << 1) | (1 & (t != nil));

		if (ttype(buf[4]) == TWord) {
			buf[4] = twrap(TWords, 1, token1(buf[4]));
		}

		if ((ttype(buf[4]) == TWords) &&
		  (ttype(buf[5]) == TWhiteSpace) &&
		  (ttype(buf[6]) == TWord)) {
			APPEND(buf[4], buf[5])
			APPEND(buf[5], buf[6])
		}

		if (buf[0] != nil) {
			send(c[1], &buf[0]);
		}
	}
	chanclose(c[1]);
}

void
pass3(void *v)
{
	Channel **c = v;
	Token *t, *b = nil;
	while(recv(c[0], &t) > 0) {
		if (b == nil) {
			if (ttype(t) == TBraceOpen) {
				b = twrap(TBraced, 1, token1(t));
			} else if (ttype(t) == TSqrBraceOpen) {
				b = twrap(TSqrBraced, 1, token1(t));
			} else send(c[1], &t);
		} else if (ttype(b) == TBraced) {
			if (ttype(t) == TBraceClose) {
				ATTACH(b->tokens, b->count, t)
				send(c[1], &b);
				b = nil;
				
			} else ATTACH(b->tokens, b->count, t)
		} else /* if (ttype(b) == TSqrBtaced) */ {
			if (ttype(t) == TSqrBraceClose) {
				ATTACH(b->tokens, b->count, t)
				send(c[1], &b);
				b = nil;
			} else ATTACH(b->tokens, b->count, t)
		}
	}
	if (b != nil) {
		fprint(2, "unclosed (square? ) brace\n");
		send(c[1], &b);
	}
	chanclose(c[1]);
}

void
link(void *v)
{
	Channel **c = v;
	Token *t, *l = nil;
	while(recv(c[0], &t) > 0) {
		if (l == nil) {
			if (ttype(t) == TSqrBraced) {
				l = t;
			} else send(c[1], &t);
		} else {
			if (ttype(t) == TBraced) {
				l = twrap(TLink, 1, token1(l));
				ATTACH(l->tokens, l->count, t)
				send(c[1], &l);
				l = nil;
			} else {
				send(c[1], &l);
				send(c[1], &t);
				l = nil;
			}
		}
	}
	if (l != nil) send(c[1], &l);
	chanclose(c[1]);
}

void
line(void *v)
{
	Channel **c = v;
	Token *t, *l = nil;
	while(recv(c[0], &t) > 0) {
		if (l == nil) {
			switch (ttype(t)) {
			case THMarker:
				l = twrap(THeader, 1, token1(t));
				break;
			case TWords:
			case TLink:
			case TQuoted:
			case TBraced:
			case TSqrBraced:
			case TWhiteSpace:
				l = twrap(TLine, 1, token1(t));
				break;
			default:
				send(c[1], &t);
			}
		}
		else switch(ttype(t)) {
		case TNewline:
		case TEmptyLine:
			send(c[1], &l);
			l = nil;
			send(c[1], &t);
			break;
		default:
			ATTACH(l->tokens, l->count, t)
		}
	}
	chanclose(c[1]);
}

void
debug(void *v)
{
	Channel **c = v;
	Token *t;
	Biobuf *b;
	b = Bfdopen(2, OWRITE);
	if (b == nil) sysfatal("debug: %r");
	while (recv(c[0], &t) > 0) {
		dbgprinttoken(b, t, 0);
		send(c[1], &t);
	}
	chanclose(c[1]);
	Bflush(b);
}

void
output(void *v)
{
	Channel **c = v;
	Token *t;
	Biobuf *b;
	b = Bfdopen(1, OWRITE);
	while (recv(c[0], &t) > 0) {
		printtoken(b, t);
		send(c[1], &t);
	}
	chanclose(c[1]);
	Bflush(b);
}

void
clear(void *v)
{
	Channel **c = v;
	Token *t;
	while (recv(c[0], &t) > 0) {
		freetoken(t);
	}
}

void
freetoken(Token *t)
{
	if (ttype(t) != TRune) 
		for (; t->count > 0; t->count--) freetoken(t->tokens[t->count - 1]);
	free(t);
}

void
dbgprinttoken(Biobuf *b, Token *t, int ind)
{
	int i;
	char *s;
	static neednl;

	for (i = 0; i < ind; i++) Bprint(b, "  ");

	neednl = 1;
	switch (t->type) {
	case TRune:
		s = tokentotext(t, 1);
		Bprint(b, "'%s'", s);
		free(s);
		break;
	case TText:
		s = tokentotext(t, 1);
		Bprint(b, "text \"%s\"", s);
		free(s);
		break;
	case THMarker:
		Bprint(b, "h_marker %d", t->count);
		break;
	case TQuoted:
		Bprint(b, "%s ", names[t->type]);
	case TWord:
		s = tokentotext(t, 1);
		Bprint(b, "\"%s\"", s);
		free(s);
		break;
	case TLine:
	case TLink:
	case TBraced:
	case TSqrBraced:
	case TWords:
		Bprint(b, "%s\n", names[t->type]);
		for (i = 0; i < t->count; i++) {
			dbgprinttoken(b, t->tokens[i], ind + 1);
		}
		break;
	default:
		Bprint(b, "%s", names[t->type]);
	}

	if (neednl > 0) {
		neednl = 0;
		Bprint(b, "\n");
	}
}

void
printtoken(Biobuf *b, Token *t)
{
	int i;
	switch(ttype(t)) {
	case TWord:
		Bprint(b, ".");
		for (i = 0; i < t->count; i++) {
			Bprint(b, "%C", trune(t->tokens[i]));
		}
		Bprint(b, "\n");
		break;
	case TLink:
		printlink(b, t);
		break;
	case TLine:
		for (i = 0; i < t->count; i++) printtoken(b, t->tokens[i]);
		Bprint(b, "n\n");
		break;
	case THeader:
		i = t->tokens[0]->count;
		if (i > 6) i = 6;
		i--;

		Bprint(b, "f%s\n", fonts[Fheader1 + i]);
		for (i = 2; i < t->count; i++) printtoken(b, t->tokens[i]);
		Bprint(b, "n\n" "f\n");
		break;
	case TEmptyLine:
		Bprint(b, "n\n");
		break;
	case TWhiteSpace:
		Bprint(b, "s\n");
		break;
	case TRune:
		Bprint(b, ".%C\n", t->rune);
		break;
	case TNewline:
		break;
	default:
		for (i = 0; i < t->count; i++) printtoken(b, t->tokens[i]);
	}
}

void
printlink(Biobuf *b, Token *t)
{
	char *text, *url;
	Token *tlink, *ttext, **tt;
	ttext = t->tokens[0];
	tlink = t->tokens[1];

	tt = findtype(ttext->tokens, ttext->count, TWords);
	if (tt == nil) {
		fprint(2, "malformed link\n");
		return;
	}
	text = tokentotext(*tt, 0);

	tt = findtype(tlink->tokens, tlink->count, TWords);
	if (tt == nil) {
		fprint(2, "malformed link\n");
		free(text);
		return;
	}
	url = tokentotext(*tt, 0);

	Bprint(b, "l%s\n", url);
	Bprint(b, ".%s\n", text);
	Bprint(b, "l\n");
	free(url);
	free(text);
}

Token **
findtype(Token **tt, int count, int type)
{
	int i;
	for (i = 0; i < count; i++) {
		if (ttype(tt[i]) == type) return tt + i;
	}
	return nil;
}

char *
tokentotext(Token *t, int escape)
{
	char *r, *s;
	int i;
	switch (t->type) {
	case TRune:
		if (escape != 0) {
			if (t->rune == L'\n') return smprint("\\n");
			if (t->rune == L'\t') return smprint("\\t");
			if (t->rune == L'"') return smprint("\\%c", '"');
		}
		return smprint("%C", t->rune);
	case TWhiteSpace:
		return smprint(" ");
	default:
		r = malloc(128);
		r[0] = '\0';
		for (i = 0; i < t->count; i++) {
			s = tokentotext(t->tokens[i], escape);
			strncat(r, s, 128);
			free(s);
		}
		return r;
	}
}

Rune
trune(Token *t)
{
	if (t == nil) return Runeerror;
	switch (t->type) {
	case TRune:
		return t->rune;
	default:
		return Runeerror;
	}
}

int
ttype(Token *t)
{
	if (t == nil) return 0;
	return t->type;
}
