#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>

char * newobj(void);
void parse(char);
void purge(void);
int setfield(char *, char *, char *);

int newblock = 1;
int header = 0;
String *bbuf;

char *root = "/mnt/richterm";

void
main(int argc, char **argv)
{
	int fd;
	Biobuf *bp;
	char c;

	if (argc > 1) {
		if ((fd = open(argv[1], OREAD)) < 0)
			sysfatal("can't open %s, %r", argv[1]);
	} else fd = 0;
	bp = Bfdopen(fd, OREAD);
	bbuf = s_new();

	while ((c = Bgetc(bp)) >= 0) {
		parse(c);
	}
}

void
parse(char c)
{
	int purgeblock;

	purgeblock = 0;

	if (newblock != 0) {
		switch(c) {
		case '#':
			header++;
			break;
		case ' ':
			if (header == 0) newblock = 0;
			break;
		default:
			newblock = 0;
		}
	} 
	if (newblock == 0){
		bbuf = s_nappend(bbuf, &c, 1);
		if (c == '\n') purgeblock = 1;
	}
	if (purgeblock != 0) {
		purge();
		newblock = 1;
		header = 0;
		/* ... */
		bbuf = s_reset(bbuf);
	}
}

void
purge(void)
{
	char *id;

	id = newobj();
	setfield(id, "text", s_to_c(bbuf));
	switch (header) {
	case 1:
		setfield(id, "font", "/lib/font/bit/lucida/unicode.20.font");
		break;
	case 2:
		setfield(id, "font", "/lib/font/bit/lucida/unicode.14.font");
		break;
	case 3:
		setfield(id, "font", "/lib/font/bit/lucida/unicode.12.font");
		break;
	}
	free(id);
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
