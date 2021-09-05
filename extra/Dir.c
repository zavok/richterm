#include <u.h>
#include <libc.h>
#include <bio.h>

char *rroot = "/mnt/richterm";

char *
getnewid(void)
{
	char *s, *id;
	int fd;
	long n;
	s = smprint("%s/new", rroot);
	fd = open(s, OREAD);
	free(s);
	if (fd < 0) sysfatal("getnewid: %r");
	id = mallocz(256, 1);
	n = read(fd, id, 256);
	if (n <= 0) sysfatal("getnewid: %r");
	return id;
}

int
rprint(char *id, char *file, char *str)
{
	int fd;
	long n;
	char *path;
	path = smprint("%s/%s/%s", rroot, id, file);
	fd = open(path, OWRITE);
	if (fd < 0) sysfatal("rprint: %r");
	n = write(fd, str, strlen(str));
	if (n < 0) sysfatal("rprint: %r");
	close(fd);
	return 0;
}

void
main(int argc, char **argv)
{
	int fd;
	long i, n;
	Dir *dp, *dbuf;
	char *path, buf[4096], *id, *spacer;
	path = getwd(buf, sizeof(buf));
	if (argc == 2) path = argv[1];
	fd = open(path, OREAD);
	if (fd < 0) sysfatal("%r");
	dp = dirfstat(fd);
	if (dp == nil) sysfatal("%r");

	id = getnewid();
	rprint(id, "text", "../\n");
	rprint(id, "link", "..");
	free(id);

	if (dp->mode & DMDIR) {
		dbuf = mallocz(DIRMAX, 1);
		n = dirreadall(fd, &dbuf);
		for (i = 0; i < n; i++) {
			char *label;
			spacer = "";
			if (dbuf[i].mode & DMEXEC) spacer = "*";
			if (dbuf[i].mode & DMDIR) spacer = "/";
			label = smprint("%s%s\n", dbuf[i].name, spacer);
			id = getnewid();
			rprint(id, "text", label);
			rprint(id, "link", dbuf[i].name);
			free(id);
		}
	} else sysfatal("not a directory");
}