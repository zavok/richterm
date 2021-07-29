#include <u.h>
#include <libc.h>
#include <bio.h>

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
main(int argc, char **argv)
{
	int fd;
	long n;
	Biobuf *bp;
	char *lp;
	if (argc > 1) {
		if ((fd = open(argv[1], OREAD)) < 0)
			sysfatal("can't open %s, %r", argv[1]);
	} else fd = 0;
	bp = Bfdopen(fd, OREAD);
	while ((lp = Brdstr(bp, '\n', 0)) != nil) {
		char *id;
		char *path;
		int td;
		id = newobj();
		path = smprint("/mnt/richterm/%s/text", id);
		print("%s ", id);
		td = open(path, OWRITE);
		if (td < 0) sysfatal("%r");
		n = write(td, lp, strlen(lp));
		if (n != strlen(lp))
			sysfatal("write failed, %ld/%ld", n, strlen(lp));
		close(td);
		free(lp);
		free(id);
		free(path);
	}
}
