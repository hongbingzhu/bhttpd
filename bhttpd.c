#include "const.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <signal.h>
#include <poll.h>

#include "strlibs.h"
#include "netlibs.h"
#include "httplibs.h"

struct serv_conf {
	char* port;
	char* pub_dir;
	char* default_page;
};

static int init_conf(struct serv_conf* conf);

int main(void)
{
	// In ubuntu 16.04, sysconf(_SC_OPEN_MAX) return 0x100000. too large for
	// program stack. for we are BASIC httpd, a small value 32 should enough.
	const int POLL_MAX = 32; //sysconf(_SC_OPEN_MAX);
	struct pollfd fds[POLL_MAX];
	int sockfd, clifd, polled = 0, i, len;
	struct serv_conf conf;
	struct mime * mime_tbl;
	struct cgi * cgi_tbl;
	struct addrinfo *info;
	struct sockaddr_storage cli_addr;
	socklen_t addr_size;
	char buff[1024];

	init_conf(&conf);
	init_info(conf.port, &info);
	mime_tbl = init_mime_table();
	cgi_tbl = init_cgi_table();

	sockfd = init_sock(info);
	if (sockfd == -1) return -1;

	memset(fds, 0, sizeof(struct pollfd)*POLL_MAX);
	for (i = 0; i < POLL_MAX; i++) fds[i].fd = -1;
	fds[0].fd = sockfd;
	fds[0].events = POLLRDNORM;


	while (1) {
		polled = poll(fds, POLL_MAX, -1);
		if (polled == -1) {
			fprintf(stderr, "poll() error\n");
			continue;
		}

		if (fds[0].revents & POLLRDNORM) {
			// Handle new connection
			clifd = accept(sockfd, (struct sockaddr *) &cli_addr, &addr_size);
			for (i = 1; i < POLL_MAX; i++) {
				if (fds[i].fd == -1) {
					fds[i].fd = clifd;
					fds[i].events = POLLRDNORM;
					break;
				}
			}
		}

		for (i = 1; i < POLL_MAX; i++) {
			if (fds[i].fd != -1 && (fds[i].revents & POLLRDNORM)) {
				handle_request(mime_tbl, cgi_tbl, conf.pub_dir, conf.default_page, fds[i].fd);
				shutdown(fds[i].fd, SHUT_WR);
				while ((len = recv(fds[i].fd, buff, sizeof(buff), 0)) > 0) fprintf(stderr, "recv before close get %d.\n", len);
				close(fds[i].fd);
				fds[i].fd = -1;
				--polled;
			}
			if (polled == 0) break;
		}
	}
	return 0;
}

static int init_conf(struct serv_conf* conf)
{
	FILE* fp;
	char buf[BUFFER_SIZE];

	if ((fp = fopen("serv.conf", "r")) == 0) {
		fprintf(stderr, "Could not read configuration file\n");
		return -1;
	}

	while (fgets(buf, BUFFER_SIZE, fp) != 0) {	// read a line, and append '\0'
		char *kvp[2];
		str_split(buf, ' ', kvp, 2);
		str_strip_tail(kvp[1]);
		int val_len = strlen(kvp[1]) + 1;

		char** pptr_conf = NULL;
		if (strcmp("PORT", kvp[0]) == 0) {
			pptr_conf = &conf->port;
		} else if (strcmp("DIRECTORY", kvp[0]) == 0) {
			pptr_conf = &conf->pub_dir;
		} else if (strcmp("DEFAULT_PAGE", kvp[0]) == 0) {
			pptr_conf = &conf->default_page;
		}
		if (pptr_conf) {
			*pptr_conf = (char*) malloc(sizeof(char) * val_len);
			strncpy(*pptr_conf, kvp[1], val_len);
		}
	}
	return 0;
}
