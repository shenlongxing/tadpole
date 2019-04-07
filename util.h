#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>
#include "sds.h"

typedef long long mstime_t; /*  millisecond time type. */

long long memtoll(const char *p, int *err);
int string2ll(const char *s, size_t slen, long long *value);
sds getAbsolutePath(char *filename);
int pathIsBaseName(char *path);
int yesnotoi(char *s);
mstime_t mstime(void);
void createPidFile(void);

#endif
