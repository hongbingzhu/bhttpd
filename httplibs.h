#ifndef HTTPLIBS_H
#define HTTPLIBS_H

#define METHOD 0
#define PATH 1
#define PROC 2
#define GET 13
#define STR_PROC "HTTP/1.1 "
#define RES_200 "200 OK\r\n"
#define RES_400 "400 Bad Request\r\n"
#define RES_404 "404 Not Found\r\n"
#define FLD_CONTENT_TYPE "Content-Type: "
#define POST 14

struct mime {
	char *ext;
	char *type;
	struct mime *next;
	struct mime *next_level;
};

struct cgi {
	char *ext;
	char *cmd;
	struct cgi *next;
};

struct request {
	int type;
	char * uri;
	char * local_path;
	char * query_string;
};

int handle_request(const struct mime *mime_tbl, const struct cgi *cgi_tbl, const char *path_prefix, const char *default_page, const int sockfd);
struct mime * init_mime_table();
struct cgi * init_cgi_table();

#endif	// HTTPLIBS_H
