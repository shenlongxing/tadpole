#ifndef _DB_H_
#define _DB_H_

#include "log.h"
#include "sds.h"
#include "zmalloc.h"
#include "dict.h"
#include "ae.h"
#include "anet.h"
#include "skiplist.h"
#include "hiredis.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

/*-----------------------------------------------------------------------------
 * Macros
 *----------------------------------------------------------------------------*/
/* Return value */
#define SERVER_ERR -1
#define SERVER_OK 0

/* Misc */
#define SERVER_KEEPALIVE_INTERVAL 60

/* Protocol and I/O related defines */
#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */

/* Client request types */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2

typedef long long mstime_t; /*  millisecond time type. */

typedef struct serverClient{
    int fd;
    size_t querybuf_peak;
    sds querybuf;
    int reqtype;            /* Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* Number of multi bulk arguments left to read. */
    int flags;
    long bulklen;           /* Length of bulk argument in multi bulk request. */
    size_t sentlen;
    int argc;
    sds *argv;
    struct server_command *cmd;
    /*  Response buffer */
    int bufpos;
    char buf[PROTO_REPLY_CHUNK_BYTES];
} client;

typedef void server_command_proc(client *c);
struct server_command {
    const char *name;
    server_command_proc *proc;
    int arity;
};


struct fixed_length {
    unsigned int key_len;
    unsigned int val_len;
};

struct dbServer {
	pid_t pid;
	int sock_fd;
	int port;
	int verbosity;  /* Loglevel in configure file */
	int daemonize;
	int repl_timeout;

	aeEventLoop *el;
	char *config_file;
	char *log_file;
	char *pidfile;
	char *db_filename;
	dict *commands;             /*  Command table */
	char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
	dict *dict;	  /* hashmap to speed up lookup existence */
	skiplist *sl; /* skiplist to score sorted kv pairs */

	struct fixed_length *fl;
	sds max_key;
};


/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/
extern struct dbServer server;
extern dictType commandTableDictType;

/*-----------------------------------------------------------------------------
 * Function declarations
 *----------------------------------------------------------------------------*/
void addReply(client *c, sds reply);
void addReplyErrorFormat(client *c, const char *fmt, ...);

void process_para(int argc, char *argv[]);
void freeClient(client *c);
void spt_init(int argc, char *argv[]);
void setproctitle(const char *fmt, ...);
void dictInstancesValDestructor(void *privdata, void *obj);
uint64_t dictSdsHash(const void *key);
int dictSdsKeyCompare(void *privdata, const void *key1, const void *key2);

#define NULLBULK sdsnew("$-1\r\n")
#define OK sdsnew("+OK\r\n")

long long ustime(void);
void addReplyString(client *c, const char *s, size_t len);
void resetClient(client *c);
#endif
