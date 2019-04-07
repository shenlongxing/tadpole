#include "db.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <float.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

unsigned int dictHash(sds key) {
    return dictGenHashFunction(key, sdslen(key));
}

int dictSdsKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

int dictKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    int cmp;

    cmp = dictSdsKeyCompare(privdata, key1, key2);
    return cmp;
}

void dictKeyDestructor(void *privdata, void *val)                                    
{
    DICT_NOTUSED(privdata);

    if (val == NULL) return; /* Values of swapped out keys as set to NULL */
    sdsfree(val);
}


static void freeClientArgv(client *c)
{
	int j;

	for (j = 0; j < c->argc; j++)
		sdsfree(c->argv[j]);
	c->argc = 0;
	c->cmd = NULL;
}


void freeClient(client *c)
{
	if (c == NULL) {
		return;
	}

	/*  Free the query buffer */
	if (c->querybuf != NULL) {
		sdsfree(c->querybuf);
		c->querybuf = NULL;
	}
	
	/* Free data structures. */
	freeClientArgv(c);
	
	/* Unregister async I/O handlers and close the socket. */
	if (c->fd != -1) {
		aeDeleteFileEvent(server.el, c->fd, AE_READABLE);
		aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
		close(c->fd);
		c->fd = -1;
	}
	
	zfree(c->argv);
	zfree(c);
	c = NULL;
	return;
}


/* Given the filename, return the absolute path as an SDS string, or NULL
 * if it fails for some reason. Note that "filename" may be an absolute path
 * already, this will be detected and handled correctly.
 *
 * The function does not try to normalize everything, but only the obvious
 * case of one or more "../" appearning at the start of "filename"
 * relative path. */
sds getAbsolutePath(char *filename) {
	char cwd[1024];
	sds abspath;
	sds relpath = sdsnew(filename);

	relpath = sdstrim(relpath," \r\n\t");
	if (relpath[0] == '/') return relpath; /* Path is already absolute. */

	/* If path is relative, join cwd and relative path. */
	if (getcwd(cwd,sizeof(cwd)) == NULL) {
		sdsfree(relpath);
		return NULL;
	}
	abspath = sdsnew(cwd);
	if (sdslen(abspath) && abspath[sdslen(abspath)-1] != '/')
		abspath = sdscat(abspath,"/");

	/* At this point we have the current path always ending with "/", and
	 * the trimmed relative path. Try to normalize the obvious case of
	 * trailing ../ elements at the start of the path.
	 *
	 * For every "../" we find in the filename, we remove it and also remove
	 * the last element of the cwd, unless the current cwd is "/". */
	while (sdslen(relpath) >= 3 &&
		   relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/')
	{
		sdsrange(relpath,3,-1);
		if (sdslen(abspath) > 1) {
			char *p = abspath + sdslen(abspath)-2;
			int trimlen = 1;

			while(*p != '/') {
				p--;
				trimlen++;
			}
			sdsrange(abspath,0,-(trimlen+1));
		}
	}

	/* Finally glue the two parts together. */
	abspath = sdscatsds(abspath,relpath);
	sdsfree(relpath);
	return abspath;
}

int yesnotoi(char *s)
{
	if (!strcasecmp(s,"yes")) return 1;
	else if (!strcasecmp(s,"no")) return 0;
	else return -1;
}

void addReply(client *c, sds reply)
{
	ssize_t nwritten = 0, total = sdslen(reply);
	ssize_t to_write;
	char *pos = reply;

	while (total > 0) {
		to_write = total > 16 * 1024 ? 16 * 1024 : total;
		nwritten = write(c->fd, pos, to_write);
		if (nwritten == -1) {
			if (errno == EAGAIN) {
				continue;
			}
		}
		total -= nwritten;
		pos += nwritten;
	}
	sdsfree(reply);
	return;
}

void addReplyString(client *c, const char *s, size_t len)
{
	write(c->fd, s, len);

	return;
}


static void addReplyErrorLength(client *c, const char *s, size_t len)
{
	addReplyString(c,"-ERR ",5);
	addReplyString(c,s,len);
	addReplyString(c,"\r\n",2);

	return;
}


void addReplyErrorFormat(client *c, const char *fmt, ...)
{
	size_t l, j;
	va_list ap;
	va_start(ap,fmt);
	sds s = sdscatvprintf(sdsempty(),fmt,ap);
	va_end(ap);

	/* Make sure there are no newlines in the string, otherwise invalid protocol
	 * is emitted. */
	l = sdslen(s);
	for (j = 0; j < l; j++) {
		if (s[j] == '\r' || s[j] == '\n') s[j] = ' ';
	}
	addReplyErrorLength(c,s,sdslen(s));
	sdsfree(s);

	return;
}


/* Convert a string into a long long. Returns 1 if the string could be parsed
 * into a (non-overflowing) long long, 0 otherwise. The value will be set to
 * the parsed value when appropriate. */
int string2ll(const char *s, size_t slen, long long *value) {
	const char *p = s;
	size_t plen = 0;
	int negative = 0;
	unsigned long long v;

	if (plen == slen)
		return 0;

	/* Special case: first and only digit is 0. */
	if (slen == 1 && p[0] == '0') {
		if (value != NULL) *value = 0;
		return 1;
	}

	if (p[0] == '-') {
		negative = 1;
		p++; plen++;

		/* Abort on only a negative sign. */
		if (plen == slen)
			return 0;
	}

	/* First digit should be 1-9, otherwise the string should just be 0. */
	if (p[0] >= '1' && p[0] <= '9') {
		v = p[0]-'0';
		p++; plen++;
	} else if (p[0] == '0' && slen == 1) {
		*value = 0;
		return 1;
	} else {
		return 0;
	}

	while (plen < slen && p[0] >= '0' && p[0] <= '9') {
		if (v > (ULLONG_MAX / 10)) /* Overflow. */
			return 0;
		v *= 10;

		if (v > (ULLONG_MAX - (p[0]-'0'))) /* Overflow. */
			return 0;
		v += p[0]-'0';

		p++; plen++;
	}

	/* Return if not all bytes were used. */
	if (plen < slen)
		return 0;

	if (negative) {
		if (v > ((unsigned long long)(-(LLONG_MIN+1))+1)) /* Overflow. */
			return 0;
		if (value != NULL) *value = -v;
	} else {
		if (v > LLONG_MAX) /* Overflow. */
			return 0;
		if (value != NULL) *value = v;
	}
	return 1;
}


#define CONFIG_DEFAULT_PID_FILE "/var/run/redis.pid"
void createPidFile(void)												  
{
	/* If pidfile requested, but no pidfile defined, use
	 * default pidfile path */
	if (!server.pidfile) {
		server.pidfile = zstrdup(CONFIG_DEFAULT_PID_FILE);
	}

	/* Try to write the pid file in a best-effort way. */
	FILE *fp = fopen(server.pidfile,"w");
	if (fp) {
		fprintf(fp,"%d\n",(int)getpid());
		fclose(fp);
	}

	return;
}

/* Return the UNIX time in microseconds */
long long ustime(void) {
	struct timeval tv;
	long long ust;
	   
	gettimeofday(&tv, NULL);
	ust = ((long long)tv.tv_sec)*1000000;
	ust += tv.tv_usec;
	return ust;
}   
	 
/* Return the UNIX time in milliseconds */
mstime_t mstime(void) {										
	return ustime()/1000;
}

uint64_t dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

sds convertToResp(sds src)
{
	sds res = sdscatfmt(sdsempty(), "$%i\r\n%S\r\n", sdslen(src), src);

	return res;
}

int pathIsBaseName(char *path) {
	return strchr(path,'/') == NULL && strchr(path,'\\') == NULL;
}
