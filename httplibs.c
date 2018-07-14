#include "const.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include "strlibs.h"
#include "netlibs.h"
#include "httplibs.h"

static int handle_cgi(const struct request *req, const struct cgi *cgi, const int sockfd);

static int parse_request_type(const char *buf);
static char * find_content_type(const struct mime *tbl, const char *ext);
static char * find_cgi_command(const struct cgi *tbl, const char *ext);
static char * determine_ext(const char *path);
static int build_cgi_env(const struct request *req);
// if have '?' in request uri
static char * has_query_string(const char *uri);

#define DEBUG(...)		fprintf(stderr, __VA_ARGS__)
#define MIN(a,b)		((a) < (b) ? (a) : (b))

int handle_request(const struct mime *mime_tbl, const struct cgi *cgi_tbl,
		const char *path_prefix, const char *default_page, const int sockfd)
{
	char buf[BUFFER_SIZE], local_path[BUFFER_SIZE];
	char basic_request[3][BUFFER_SIZE], *content_type = 0, *query = 0;
	struct request req;
	int type = 0, line_len;
	memset(buf, 0, sizeof buf);
	memset(local_path, 0, sizeof local_path);
	memset(&req, 0, sizeof(struct request));

	if ((line_len = read_line(buf, sizeof(buf), sockfd)) > 0) {
		sscanf(buf, "%s %s %s", basic_request[METHOD], basic_request[PATH], basic_request[PROC]);

		type = parse_request_type(basic_request[METHOD]);

		query = has_query_string(basic_request[PATH]);
		if (query) {
			*query = 0;
			++query;
		}

		/* Add default page */
		if (strlen(basic_request[PATH]) == 1 && basic_request[PATH][0] == '/') {
			strcat(basic_request[PATH], default_page);
		}

		strncat(local_path, path_prefix, BUFFER_SIZE - 1);
		strncat(local_path, basic_request[PATH], BUFFER_SIZE - 1);

		req.type = type;
		req.uri = basic_request[PATH];
		req.local_path = local_path;
		req.query_string = query;

		//const time_t t = time(0);
		//DEBUG("[%s] %s %s, type=%d\n", ctime(&t), basic_request[METHOD], basic_request[PATH], type);
	} else if (line_len == 0) {
		return -1;
	} else {
		//DEBUG("read_line() get length %d. Leading n(<=8) bytes is", line_len);
		//for (int i = 0; i < MIN(8,line_len); i++) DEBUG(" %02x", buf[i]);
		//DEBUG("\n");
		return -1;
	}

	write_socket(STR_PROC, strlen(STR_PROC), sockfd);
	if (type == GET) {
		FILE *fp = fopen(local_path, "r");
		if (fp == 0) {
			// File doesn't exist
			write_socket(RES_404, strlen(RES_404), sockfd);
			write_socket("\r\n", 2, sockfd);
			return 0;
		}
		fclose(fp);

		write_socket(RES_200, strlen(RES_200), sockfd);
		if (handle_cgi(&req, cgi_tbl, sockfd) == 0) {
			content_type = find_content_type(mime_tbl, determine_ext(req.local_path));
			write_socket(FLD_CONTENT_TYPE, strlen(FLD_CONTENT_TYPE), sockfd);
			write_socket(content_type, strlen(content_type), sockfd);
			write_socket("\r\n", 2, sockfd);
			write_socket("\r\n", 2, sockfd);
			send_file(local_path, sockfd);
			write_socket("\r\n", 2, sockfd);
		}
	} else if (type == POST) {
		if (handle_cgi(&req, cgi_tbl, sockfd) <= 0) {
			return -1;
		}
	} else {
		write_socket(RES_400, strlen(RES_400), sockfd);
		write_socket("\r\n", 2, sockfd);
		write_socket("\r\n", 2, sockfd);
	}
	return 0;
}

static int handle_cgi(const struct request *req, const struct cgi *cgi, const int sockfd)
{
	char *cmd;
	char buf[BUFFER_SIZE];
	int cp[2], fork_stat;

	if (cgi == 0) return 0;

	cmd = find_cgi_command(cgi, determine_ext(req->local_path));
	if (cmd == 0) return 0;

	build_cgi_env(req);

	if (req->type == GET) {
		if (req->query_string) {
			setenv("CONTENT_LENGTH", "", 1);
			setenv("QUERY_STRING", req->query_string, 1);
		}

		if (pipe(cp) < 0) {
			fprintf(stderr, "Cannot pipe\n");
			return -1;
		}

		pid_t pid;
		if ((pid = vfork()) == -1) {
			fprintf(stderr, "Failed to fork new process\n");
			return -1;
		}

		if (pid == 0) {
			close(sockfd);
			close(cp[0]);
			dup2(cp[1], STDOUT_FILENO);
			execlp(cmd, cmd, req->local_path, (char*) 0);
			exit(0);
		} else {
			close(cp[1]);
			int len;
			while ((len = read(cp[0], buf, BUFFER_SIZE)) > 0) {
				buf[len] = '\0';
				str_strip_tail(buf);
				len = strlen(buf);
				write_socket(buf, len, sockfd);
			}
			close(cp[0]);
			waitpid((pid_t)pid, &fork_stat, 0);
		}
	} else if (req->type == POST) {
		int post_pipe[2];
		char content_len[BUFFER_SIZE];
		write_socket(RES_200, strlen(RES_200), sockfd);
		memset(buf, 0, sizeof buf);

		while (read_line(buf, sizeof(buf), sockfd)) {
			if (buf[0] == '\r') break;
			char *ptr = buf;
			while (*ptr != ':') ++ptr;
			*ptr = 0;
			if (strcmp("Content-Type", buf) == 0) {
				setenv("CONTENT_TYPE", str_strip_tail(ptr += 2), 1);
			}
			memset(buf, 0, sizeof buf);
		}

		memset(buf, 0, sizeof buf);
		read_socket(buf, BUFFER_SIZE, sockfd);
		sprintf(content_len, "%d", (int) strlen(buf));
		setenv("CONTENT_LENGTH", content_len, 1);

		setenv("QUERY_STRING", buf, 1);

		if (pipe(cp) < 0 || pipe(post_pipe) < 0) {
			fprintf(stderr, "Cannot pipe\n");
			return -1;
		}

		pid_t pid;
		if ((pid = vfork()) == -1) {
			fprintf(stderr, "Failed to fork new process\n");
			return -1;
		}

		if (pid == 0) {
			close(sockfd);
			close(cp[0]);
			close(post_pipe[1]);
			dup2(post_pipe[0], STDIN_FILENO);
			close(post_pipe[0]);
			dup2(cp[1], STDOUT_FILENO);
			execlp(cmd, cmd, req->local_path, (char*) 0);
			exit(0);
		} else {
			close(post_pipe[0]);
			close(cp[1]);
			write(post_pipe[1], buf, strlen(buf) + 1);
			close(post_pipe[1]);
			memset(buf, 0, sizeof buf);
			int len;
			while ((len = read(cp[0], buf, BUFFER_SIZE)) > 0) {
				buf[len] = '\0';
				str_strip_tail(buf);
				len = strlen(buf);
				write_socket(buf, len, sockfd);
			}
			waitpid((pid_t)pid, &fork_stat, 0);
			close(cp[0]);
			write_socket("\r\n", 2, sockfd);
			write_socket("\r\n", 2, sockfd);
		}

	}

	return 1;
}

static int parse_request_type(const char *buf)
{
	if (strcmp(buf, "GET") == 0) {
		return GET;
	} else if (strcmp(buf, "POST") == 0) {
		return POST;
	}
	return 0;
}

struct mime * init_mime_table()
{
	// Base items for root, level 1, level 2
	static struct mime s_baseItems[1+26+26*26];

	char buf[BUFFER_SIZE], ext[BUFFER_SIZE], type[BUFFER_SIZE];
	struct mime item;
	struct mime *root = 0;
	FILE *fp = fopen("mime.types", "r");
	int i;

	if (fp == 0) {
		fprintf(stderr, "Cannot open mime.types\n");
		fclose(fp);
		return 0;
	}

	/* Initialize two-level structure */
	memset(s_baseItems, 0, sizeof(s_baseItems));
	root = &s_baseItems[0];
	root->next_level = &s_baseItems[1];
	for (i = 0; i < 26; i++)
		root->next_level[i].next_level = &s_baseItems[1+26+26*i];

	memset(buf, 0, sizeof buf);
	while (fgets(buf, BUFFER_SIZE, fp) != 0) {
		sscanf(buf, "%s %s", ext, type);

		/* Create new node in advance */
		memset(&item, 0, sizeof(struct mime));
		item.ext = (char*) malloc(sizeof(char) * (strlen(ext) + 1));
		item.type = (char*) malloc(sizeof(char) * (strlen(type) + 1));
		strcpy(item.ext, ext);
		strcpy(item.type, type);

		struct mime *level1 = &(root->next_level[ext[0] - 'a']);
		struct mime *start = (strlen(ext) == 1) ? level1 :
				&(level1->next_level[ext[1] - 'a']);

		if (start->ext == 0) {
			/* Use existing node if the node is empty */
			start->ext = item.ext;
			start->type = item.type;
		} else {
			/* Append previously created node to last */
			while (start->next != 0) start = start->next;
			struct mime *ptr = (struct mime*) malloc(sizeof(struct mime));
			*ptr = item;
			start->next = ptr;
		}
		memset(buf, 0, sizeof buf);
	}

	fclose(fp);
	return root;
}

static char * find_content_type(const struct mime *tbl, const char *ext)
{
	struct mime *first_level = &(tbl->next_level[ext[0] - 'a']);
	struct mime *second_level = &(first_level->next_level[ext[1] - 'a']);
	struct mime *start = (strlen(ext) == 1) ? first_level : second_level;

	while (start) {
		if (strcmp(ext, start->ext) == 0) {
			return start->type;
		}
		start = start->next;
	}
	return 0;
}

static char * find_cgi_command(const struct cgi *tbl, const char *ext)
{
	while (tbl != 0) {
		if (strcmp(tbl->ext, ext) == 0) return tbl->cmd;
		tbl = tbl->next;
	}
	return 0;
}

static char * determine_ext(const char *path)
{
	const int len = strlen(path);
	path += len - 1;

	while (*path != '.') --path;
	return (char*)(path + 1);
}

static int build_cgi_env(const struct request *req)
{
	const char* local_path = req->local_path;
	const char* uri = req->uri;
	const int req_type = req->type;

	setenv("SERVER_NAME", "localhost", 1);
	setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
	setenv("REQUEST_URI", uri, 1);
	setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
	setenv("REDIRECT_STATUS", "200", 1);
	setenv("SCRIPT_FILENAME", local_path, 1);
	setenv("SCRIPT_NAME", uri, 1);

	if (req_type == GET) {
		setenv("REQUEST_METHOD", "GET", 1);
	} else if (req_type == POST) {
		setenv("REQUEST_METHOD", "POST", 1);
	}

	return 0;
}

static char * has_query_string(const char *uri)
{
	while (*uri != 0) {
		if (*uri == '?') return (char *) uri;
		++uri;
	}

	return 0;
}

struct cgi * init_cgi_table()
{
	char buf[BUFFER_SIZE];
	struct cgi *ptr = 0, *root = 0;
	FILE *fp;

	if ((fp = fopen("cgi.conf", "r")) == 0) {
		fprintf(stderr, "Cannot open cgi.conf\n");
		return 0;
	}

	while (fgets(buf, BUFFER_SIZE, fp) != 0) {
		str_strip_tail(buf);
		if (strcmp("[CGI]", buf) != 0) {
			return 0;
		}
		int i;
		if (ptr == 0) {		// first item, assign as root
			ptr = (struct cgi*) malloc(sizeof(struct cgi));
			root = ptr;
		} else {			// else, chain to tail
			ptr->next = (struct cgi*) malloc(sizeof(struct cgi));
			ptr = ptr->next;
		}
		ptr->next = 0;
		for (i = 0; i < 2; i++) {
			if (fgets(buf, BUFFER_SIZE, fp) == 0) {
				free(ptr);
				return 0;
			}
			char *kvp[2];
			str_split(buf, ' ', kvp, 2);
			str_strip_tail(kvp[1]);

			int val_len = strlen(kvp[1]) + 1;
			if (strcmp("EXTNAME", kvp[0]) == 0) {
				ptr->ext = (char*) malloc(sizeof(char) * val_len);
				strncpy(ptr->ext, kvp[1], val_len);
			} else if (strcmp("CMD", kvp[0]) == 0) {
				ptr->cmd = (char*) malloc(sizeof(char) * val_len);
				strncpy(ptr->cmd, kvp[1], val_len);
			}
		}
	}

	fclose(fp);
	return root;
}
