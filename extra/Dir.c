#include <u.h>
#include <libc.h>
#include <bio.h>

char *rroot = "/mnt/richterm";

void
main(int argc, char **argv)
{
	int fd, tfd;
	long i, n;
	Dir *dp, *dbuf;
	char *path, buf[4096];

	path = getwd(buf, sizeof(buf));

	if (argc == 2) path = argv[1];

	fd = open(path, OREAD);
	if (fd < 0) sysfatal("%r");

	dp = dirfstat(fd);
	if (dp == nil) sysfatal("%r");

	if (dp->mode & DMDIR) {

		char *rpath = smprint("%s/text", rroot);
		tfd = open(rpath , OWRITE);
		if (tfd < 0) sysfatal("can't open %s: %r", rpath);

		dbuf = mallocz(DIRMAX, 1);
		n = dirreadall(fd, &dbuf);

		seek(tfd, 0, 2);
		fprint(tfd, "l\nf\n");

		for (i = 0; i < n; i++) {
			char *spacer = "";
			if (dbuf[i].mode & DMEXEC) spacer = "*";
			if (dbuf[i].mode & DMDIR) spacer = "/";

			fprint(tfd, "l%s\n.%s%s\nn\n", dbuf[i].name, dbuf[i].name, spacer);
		}
		fprint(tfd, "l\n");
	} else sysfatal("not a directory");
}
