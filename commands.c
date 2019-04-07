#include "commands.h"
#include "hiredis.h"
#include "log.h"
#include "sds.h"
#include "db.h"
#include "skiplist.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/socket.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>

/* function declaration */
static void pingCommand(client *c);
static void infoCommand(client *c);
static void shutdownCommand(client *c);
static int getCommand(client *c);
static int putCommand(client *c);
static void deleteCommand(client *c);
static void scanCommand(client *c);

/* Migrate cache dict type. */
dictType commandTableDictType = {
	dictSdsCaseHash,            /* hash function */
	NULL,                       /* key dup */
	NULL,                       /* val dup */
	dictSdsKeyCaseCompare,      /* key compare */
	dictSdsDestructor,          /* key destructor */
	NULL                        /* val destructor */
};


struct server_command server_commands_table[] = {
	{"get",      getCommand,       2}, 
	{"put",	     putCommand,       3}, 
	{"set",	     putCommand,       3}, 
	{"delete",   deleteCommand,    2}, 
	{"scan",     scanCommand,      3},
	{"ping",     pingCommand,      1},
	{"shutdown", shutdownCommand,  1},
	{"show",     infoCommand,      1},
	{NULL,       NULL,             0}, 
};

/* Populates the Redis Command Table starting from the hard coded list
 * we have on top of redis.c file. */
void populateCommandTable()
{
	int retval;

	struct server_command *ptr = server_commands_table;
	while (ptr->name != NULL) {
		retval = dictAdd(server.commands, sdsnew(ptr->name), ptr);
		assert(retval == DICT_OK);
		ptr++;
	}

	return;
}

struct server_command* lookupCommand(sds name)
{ 
	return dictFetchValue(server.commands, name);
}

unsigned int dictSdsCaseHash(const void *key)
{
	return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1, const void *key2)
{
	DICT_NOTUSED(privdata);

	return strcasecmp(key1, key2) == 0;
}


void dictSdsDestructor(void *privdata, void *val)
{
	DICT_NOTUSED(privdata);

	sdsfree(val);
	return;
}


static void pingCommand(client *c)
{
	addReply(c, sdsnew("+PONG\r\n"));

	return;
}

static int getCommand(client *c)
{
	/* check key length */
	if (server.fl) {
		if (sdslen(c->argv[1]) != server.fl->key_len) {
			addReplyErrorFormat(c, "Illegal key length, key length should be %ld", 
									server.fl->key_len);
			return 0;
		}
	}

	/* search from dict */
	if (dictFind(server.dict, c->argv[1]) == NULL) {
		addReply(c, NULLBULK);
		return 0;
	}

	sds res = search_skiplist(server.sl, c->argv[1]);

	addReply(c, convertToResp(res));
	return 0;
}

static int putCommand(client *c)
{
	dictEntry *de;

	/* check key/value length */
	if (server.fl) {
		if (sdslen(c->argv[1]) != server.fl->key_len || 
			sdslen(c->argv[2]) != server.fl->val_len) {
			addReplyErrorFormat(c, "Illegal kv length, key/value length should be %ld/%ld", 
									server.fl->key_len, server.fl->val_len);
			return 0;
		}
	}

	de = dictFind(server.dict, c->argv[1]);
	if (de) {
		/* if kv already exists, just replace ziplist node */
		replace_skiplist(server.sl, c->argv[1], c->argv[2]);
	} else {
		/* just add raw key to dict */
		dictAddRaw(server.dict, sdsdup(c->argv[1]));	
		/* insert into skiplist */
		insert_skiplist(server.sl, c->argv[1], c->argv[2]);
	}

	addReply(c, OK);
	return 0;
}

static void deleteCommand(client *c)
{
	/* check key length */
	if (server.fl) {
		if (sdslen(c->argv[1]) != server.fl->key_len) {
			addReplyErrorFormat(c, "Illegal key length, key length should be %ld", 
									server.fl->key_len);
			return 0;
		}
	}

	dictEntry *de;
	de = dictFind(server.dict, c->argv[1]);
	if (!de) {
		addReply(c, sdsnew("+0\r\n"));
	} else {
		delete_skiplist(server.sl, c->argv[1]);
		dictDelete(server.dict, c->argv[1]);
		addReply(c, sdsnew("+1\r\n"));
	}
	

	return;
}

static void scanCommand(client *c)
{
	sds start = c->argv[1];
	sds end = c->argv[2];

	/* check key length */
	if (server.fl) {
		if (sdslen(start) != server.fl->key_len || 
			sdslen(end) != server.fl->key_len) {
			addReplyErrorFormat(c, "Illegal cursor length, key length should be %ld", 
									server.fl->key_len);
			return 0;
		}
	}

	/* check start/end cursor */
	if (slKeyCompare(start, end) > 0) {
		addReplyErrorFormat(c, "CURSORERR '%s' should less or equal to '%s'", start, end);
		return;
	}

	unsigned long numkeys = 0;
	sl_node *node = server.sl->head->next[0];
	sds tmp = sdsempty();
	while (node) {
		if (slKeyCompare(node->key, start) >= 0 && 
			slKeyCompare(node->key, end) <= 0) {

			tmp = sdscatfmt(tmp, "%S\n", node->key);
			numkeys++;
		}

		node = node->next[0];
	}

	addReplyString(c, "+", 1);
	/* drop the last '\n' */
	sdsrange(tmp, 0, -2);
	addReply(c, tmp);
	addReply(c, sdsnew("\r\n"));
	return;
}

/* 
 * info command to show server status:
 * min/max key and key number
 */
static void infoCommand(client *c)
{
	sds info;

	info = sdscatfmt(sdsempty(), 
	    "tadpole:keys=%i,min=%S,max=%S", 
		server.sl->length,
		server.sl->length == 0 ? sdsnew("NULL"):server.sl->head->next[0]->key,
		server.sl->length == 0 ? sdsnew("NULL"):find_max_skiplist(server.sl));

	addReplyString(c, "+", 1);
	addReply(c, info);
	addReply(c, sdsnew("\r\n"));

	return;
}


static void shutdownCommand(client *c)
{
	server_log(LL_WARNING, "tadpole is now ready to exit, bye bye...");
	/* exit immediately */
	exit(0);
	
}
