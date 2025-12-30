#ifndef HTTP_H
#define HTTP_H

#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <arpa/inet.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

extern bool http_init(const char*url);
extern void http_setRawHeader(char*name,char*value);
extern void http_get();
extern void http_post(char*jsonData);
extern bool http_reply(char**p);

extern void http_destory();

#endif // HTTP_H
