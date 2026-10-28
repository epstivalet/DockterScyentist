#ifndef RETROVERSE_CURL_NET_H
#define RETROVERSE_CURL_NET_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
int rv_https_get_memory(const char *url,size_t max,unsigned char **out,size_t *size,
                        char *err,size_t errlen);
int rv_https_download(const char *url,FILE *destination,uint64_t max,
                      volatile int *cancel,volatile uint64_t *received,
                      volatile uint64_t *total,char *err,size_t errlen);
#endif
