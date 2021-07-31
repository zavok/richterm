#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>

char * newobj(void);
void parse(char);
void purge(void);

int newblock = 1;
String *bbuf;

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
		newblock = 0;
		/* ... */
	} else {
		/* ... */
	}
	
	bbuf = s_nappend(bbuf, &c, 1);

	if (c == '\n') purgeblock = 1;

	if (purgeblock != 0) {
		newblock = 1;
		/* ... */
		purge();
		bbuf = s_reset(bbuf);
	}
}

void
purge(void)
{
	char *id, *path;
	int td;
	usize n, size;

	id = newobj();
	path = smprint("/mnt/richterm/%s/text", id);

	td = open(path, OWRITE);
	if (td < 0) sysfatal("%r");

	size = bbuf->end - bbuf->base;

	n = write(td, bbuf->base, size);

	if (n != size)
		sysfatal("write failed: %r");
	close(td);
	free(path);
	
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
