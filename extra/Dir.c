#include <u.h>
#include <libc.h>
#include <bio.h>

char *rroot = "/mnt/richterm";

void
main(int argc, char **argv)
{
	int fd;
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

		dbuf = mallocz(DIRMAX, 1);
		n = dirreadall(fd, &dbuf);

		print("f\n" "l..\n" ".../\n" "l\n" "n\n");

		for (i = 0; i < n; i++) {
			char *spacer = "";
			if (dbuf[i].mode & DMEXEC) spacer = "*";
			if (dbuf[i].mode & DMDIR) spacer = "/";

			print("l%s\n" ".%s%s\n" "l\n" "n\n", dbuf[i].name, dbuf[i].name, spacer);
		}
	} else sysfatal("not a directory");
}
