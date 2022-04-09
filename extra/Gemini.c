#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <bio.h>

#include "config.h"

#define BSIZE 4096

char *host;
char * gethost(char *uri);
void printheader(char *line);
void printlink(char *);
void getbody(char *header, Biobuf *bfd);

void
usage(void)
{
	fprint(2, "usage: %s [-h host] uri\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *header, *uri;
	long n;
	int fd, tlsfd;
	Biobuf bfd;
	TLSconn conn;

	host = nil;

	ARGBEGIN {
	case 'h':
		host = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (argc != 1) usage();

	uri = argv[0];
	if (host == nil) host = gethost(uri);
	if (host == nil) sysfatal("gethost: %r");

	fd = dial(netmkaddr(host, "tcp", "1965"), nil, nil, nil);
	if (fd < 0) sysfatal("dial: %r");

	memset(&conn, 0, sizeof(conn));
	conn.serverName = host;
	tlsfd = tlsClient(fd, &conn);
	if (tlsfd < 0) sysfatal("tlsClient: %r");

	if (Binit(&bfd, tlsfd, OREAD) < 0)
		sysfatal("Binit: %r");

	n = fprint(tlsfd, "%s\r\n", uri);
	if (n < 0) sysfatal("fprint: %r");

	header = Brdstr(&bfd, '\n', 1);
	n = Blinelen(&bfd);
	if (n > 1024 + 2)
		fprint(2, "warning: header too long\n");
	header[n-1] = '\0';

	enum {
		SCInput = 10,
		SCSensitiveInput = 11,
		SCSuccess = 20,
		SCRedirect = 30,
		SCPermRedirect = 31,
	};

	switch (atoi(header)) {
	case SCInput:
	case SCSensitiveInput:
		// TODO:ask for input
		print(".%s\n" "n\n" ".input is not supported\n" "n\n", header);
		break;
	case SCSuccess:
		getbody(header, &bfd);
		break;
	case SCRedirect:
	case SCPermRedirect:
		print(".Redirect: \n" "l%s\n" ".%s\n" "n\n" "l\n", header + 3, header + 3);
		break;
	default:
		// print header
		print(".%s\n" "n\n", header);
	}

	Bterm(&bfd);
	close(tlsfd);
	close(fd);
}


char *
gethost(char *uri)
{
	char *host, *sp, *ep;
	long length;

	sp = strstr(uri, "://");
	if (sp == nil) {
		werrstr("missing scheme:// in '%s'", uri);
		return nil;
	}
	else sp += 3;
	
	ep = strchr(sp, '/');
	if (ep == nil) ep = sp + strlen(sp);
	length = ep - sp;
	host = mallocz(sizeof(char) * (length+1), 1);
	memcpy(host, sp, length);
	return host;
}

void
getbody(char *header, Biobuf *bfd)
{
	char *line;
	int preform = 0;

	if (strncmp(header + 3, "text", 4) == 0) {

		while ((line = Brdstr(bfd, '\n', 1)) != nil) {
			long n = Blinelen(bfd);
			if (line[n-1] == '\r') line[n-1] = '\0';
			if (line[0] == '#') printheader(line);
			else if (strncmp(line, "```", 3) == 0) {
				preform = !preform;
				if (preform == 1) print("%s", fonts[Fcode]);
				else print("f\n");
			} else if (strncmp(line, "=>", 2) == 0) {
				printlink(line);
			} else {
				print(".%s\n" "n\n", line);
			}
			free(line);
		}
		if (preform == 1) print("f\n");

	} else {
		print(".%s\n" "n\n", header);
	}
}

void
printheader(char *line)
{
	int i = 0;
	while (line[i] == '#') i++;
	char *font = (i > 3) ? fonts[Fheader6] : fonts[Fheader1  + i - 1];
	while ((line[i] == ' ') | (line[i] == '\t')) i++;
	print("f%s\n" ".%s\n" "n\n" "f\n", font, line + i);
}

void
printlink(char *line)
{
	char *p, phost[1024];

	for (p = line + 2; (*p == ' ') || (*p == '\t'); p++) if (*p == '\0') return;
	line = strpbrk(p, " \t");

	if (line != nil) {
		*line = '\0';
		line++;
		for (; (*line == ' ') || (*line == '\t'); line++);
	} else line = p;

	phost[0] = '\0';
	if (strstr(p, "://") == nil) {
		strcat(phost, "gemini://");
		strcat(phost, host);
		if (p[0] != '/') strcat(phost, "/");
	}

	print("l%s%s\n" ".%s\n" "l\n" "n\n", phost, p, line);
}
