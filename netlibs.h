#ifndef NETLIBS_H
#define NETLIBS_H

#include <netdb.h>

int init_info(const char* port, struct addrinfo** serv);
int init_sock(const struct addrinfo *info);
int send_file(const char *file_path, const int sockfd);
int read_line(char *buf, int max_len, const int sockfd);
int write_socket(const char *buf, const int len, const int sockfd);
int read_socket(char *buf, int len, const int sockfd);

#endif	// NETLIBS_H
