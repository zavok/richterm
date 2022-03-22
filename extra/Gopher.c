#include <u.h>
#include <libc.h>
#include <bio.h>

const char *scheme = "gopher://";
char *rroot = "/mnt/richterm";

void
usage(void)
{
	fprint(2, "usage: %s [gopher_url]\n", argv0);
	exits("usage");
}

void
printtext(char *buf, long size)
{
	Biobuf* bd;
	long n;
	bd = Bfdopen(1, OWRITE);
	Bprint(bd, "f\nl\n" ".");
	for (n = 0; n < size; n++) {
		if (buf[n] == '\r') continue;
		else if (buf[n] == '\n') Bprint(bd, "\n" "n\n" ".");
		else Bputc(bd, buf[n]);
	}
	Bprint(bd, "\n");
}

void
printmenu(char *buf, long size)
{
	char *lbuf, *ls, *le, type, *port, *host, *path, url[1024];
	long lsize, n;
	int urlblank;

	ls = buf;
	lsize = 1024;
	lbuf = malloc(lsize);
	urlblank = 0;
	while (ls < buf + size) {
		le = strchr(ls, '\n');
		if (le == nil) break;
		n = le - ls;
		if (lsize < n) {
			lsize = n;
			free(lbuf);
			lbuf = malloc(lsize);
		}
		memcpy(lbuf, ls, n);
		lbuf[n - 1] = '\0';
		type = lbuf[0];
		ls = le + 1;
		if (type == '.') break;
		le = strchr(lbuf, '\t');
		if (le == nil) return; // sysfatal("gopher menu is malformed\n");
		*le = '\0';
		switch (type) {
		case 'i':
		case 'e':
			url[0] = '\0';
			break;
		default:
			path = le + 1;
			le = strchr(path, '\t');
			*le = '\0';
			host = le + 1;
			le = strchr(host, '\t');
			*le = '\0';
			port = le + 1;
			snprint(url, 1024, "gopher://%s:%s/%c%s", host, port, type, path);
		}

		if (url[0] != '\0') {
			print("l%s\n", url);
			urlblank = 0;
		} else if (urlblank == 0) {
			print("l\n");
			urlblank = 1;
		}

		if (lbuf[1] != '\0') {
			print(".%s\n", lbuf + 1);
		}

		print("n\n");
	}
}

void
main(int argc, char **argv)
{
	char *url, *up, *host, *port, type, *path, *netaddr, *buf;
	int fd;
	long n, size;
	void (*printfunc) (char *, long);
	argv0 = argv[0];
	printfunc = nil;
	if ((argc == 1) || (argc > 2)) usage();
	url = argv[1];
	if (strncmp(scheme, url, strlen(scheme)) != 0) {
		fprint(2, "%s: invalid url\n", url);
		exits("invalid url");
	}
	type = '1';
	path = "/";
	port = "70";
	url = url + strlen(scheme);
	up = strchr(url, '/');
	if (up != nil) {
		host = mallocz(up - url + 1, 1);
		memcpy(host, url, up - url);
		up++;
		if (up < url + strlen(url)) {
			type = *(up);
			path = ++up;
		}
		up = strchr(host, ':');
		if (up != nil) {
			*up = '\0';
			port = up + 1;
		}
	} else {
		host = url;
	}
	switch (type) {
	case '0':
		printfunc = printtext;
		break;
	case '1':
		printfunc = printmenu;
		break;
	default:
		sysfatal("unknown gopher type: %c\n", type);
	}
	netaddr = netmkaddr(host, "tcp", port);
	fd = dial(netaddr, nil, nil, nil);
	if (fd < 0) {
		fprint(2, "url: %s\n", argv[1]);
		fprint(2, "netaddr: %s\n", netaddr);
		sysfatal("dial: %r");
	}
	write(fd, path, strlen(path));
	write(fd, "\n", 1);
	buf = malloc(4096);
	size = 0;
	while((n = read(fd, buf + size, 4096)) > 0) {
		size += n;
		buf = realloc(buf, size + 4096);
	}
	close(fd);
	printfunc(buf, size);
}
