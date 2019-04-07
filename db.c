#include "commands.h"
#include "config.h"
#include "dict.h"
#include "db.h"
#include "log.h"
#include "util.h"

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(V) ((void) V)
#define CONFIG_DEFAULT_SERVER_PORT   6666
#define CONFIG_MAX_LINE              1024
#define MAX_EVENT_SIZE               1024


struct dbServer server;

/* Sorted sets hash (note: a skiplist is used in addition to the hash table) */
dictType slDictType = {
    dictSdsHash,               /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCompare,         /* key compare */
	dictSdsDestructor,		   /* key destructor */
    NULL                       /* val destructor */
};

static int usage()
{
	fprintf(stderr,"Usage: ./tadpole [-c /path/to/server.conf]\n");
	fprintf(stderr,"       ./tadpole -v or --version\n");
	fprintf(stderr,"       ./tadpole -h or --help\n");

	exit(1);
}

static int version()
{
	printf("tadpole version=%s\n", "1.0.0");
	exit(0);
}


void process_para(int argc, char *argv[])
{
	/* process argument */
	if (argc == 2) {
		if (strcasecmp(argv[1], "-v") == 0 ||
			strcasecmp(argv[1], "--version") == 0) {
			version();
		}
	
		if (strcasecmp(argv[1], "-h") == 0 ||
			strcasecmp(argv[1], "--help") == 0) {
			usage();
		}

		usage();
	} else if (argc == 3) {
		/* tadpole [-c /path/to/tadpole.conf] */
		if (strcasecmp(argv[1], "-c") == 0) {
			return;
		} else {
			usage();
		}
	} else {
		usage();
	}

	return;
}

/* initialize server configuration */
void init_config()
{
	server.sock_fd = -1;
	server.port = CONFIG_DEFAULT_SERVER_PORT;
	server.verbosity = CONFIG_DEFAULT_VERBOSITY;
	server.config_file = NULL;
	server.log_file = NULL;
	server.commands = dictCreate(&commandTableDictType, NULL);

	return;
}


void load_config(void)
{
	sds config = sdsempty();
	char buf[CONFIG_MAX_LINE+1];

	if (server.config_file) {
		FILE *fp;
		if ((fp = fopen(server.config_file, "r")) == NULL) {
			fprintf(stderr, "Open config file %s error\n", server.config_file);
			exit(1);
		}

		while (fgets(buf, CONFIG_MAX_LINE+1, fp) != NULL) {
			config = sdscat(config, buf);
		}
		fclose(fp);

		loadServerConfigFromString(config);
		sdsfree(config);
	} else {
		server_log(LL_WARNING, "Config file does not exist.");
		exit(1);
	}

	return;
}

static void daemonize()
{
	int fd;
	
	if (fork() != 0) exit(0); /* parent exits */
	setsid(); /* create a new session */
	
	/* Every output goes to /dev/null. If Redis is daemonized but
	 * the 'logfile' is set to 'stdout' in the configuration file
	 * it will not log at all. */
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) close(fd);
	}

	return;
}

static void sigShutdownHandler(int sig)
{
	char *msg;

	switch (sig) {
	case SIGINT:
		msg = "Received SIGINT scheduling shutdown...";
		break;
	case SIGTERM:
		msg = "Received SIGTERM scheduling shutdown...";
		break;
	default:
		msg = "Received shutdown signal, scheduling shutdown...";
	};

	exit(0);

}


static void sigHandler(int sig)
{
	switch (sig) {
	case SIGCHLD:
		sigShutdownHandler(sig);
		//sigChildHandler(sig);
		break;
	default:
		sigShutdownHandler(sig);
	}

	return;
}

static void setupSignalHandlers(void)
{
	struct sigaction act;

	/* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
	 * Otherwise, sa_handler is used. */
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sigHandler;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGCHLD, &act, NULL);

	return;
}

#define MAX_BACKLOG_SIZE 128
static int listenToPort(int port)
{
	int fd = 0;

	fd = anetTcpServer(server.neterr, port, NULL, MAX_BACKLOG_SIZE);
	if (fd == ANET_ERR) {
		fprintf(stderr, "Create socket server fail\n");
		return SERVER_ERR;
	}

	anetNonBlock(NULL, fd);
	return fd;
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * If SERVER_OK is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if SERVER_ERR is returned the client was destroyed (i.e. after QUIT). */
int processCommand(client *c) {
	/* The QUIT command is handled separately. Normal command procs will
	 * go through checking for replication and QUIT will cause trouble
	 * when FORCE_REPLICATION is enabled and would be implemented in
	 * a regular command proc. */
	if (!strcasecmp(c->argv[0],"quit")) {
		addReply(c, sdsnew("+OK\r\n"));
		return SERVER_ERR;
	}

	/* Now lookup the command and check ASAP about trivial error conditions
	 * such as wrong arity, bad command name and so forth. */
	c->cmd = lookupCommand(c->argv[0]);
	if (!c->cmd) {
		addReplyErrorFormat(c,"unknown command '%s'",
			(char*)c->argv[0]);
		return SERVER_OK;
	} else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
			   (c->argc < -c->cmd->arity)) {
		addReplyErrorFormat(c,"wrong number of arguments for '%s' command",
			c->cmd->name);
		return SERVER_OK;
	}

	/* Exec the command */
	c->cmd->proc(c);

	return SERVER_OK;
}

static int processInlineBuffer(client *c)
{
	char *newline;
	int argc, j;
	sds *argv, aux;
	size_t querylen;

	/* Search for end of line */
	newline = strchr(c->querybuf,'\n');

	/* Nothing to do without a \r\n */
	if (newline == NULL) {
		if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
		// TODO
#if 0
			addReplyError(c,"Protocol error: too big inline request");
			setProtocolError(c,0);
#endif
		}
		return SERVER_ERR;
	}

	/* Handle the \r\n case. */
	if (newline && newline != c->querybuf && *(newline-1) == '\r')
		newline--;

	/* Split the input buffer up to the \r\n */
	querylen = newline-(c->querybuf);
	aux = sdsnewlen(c->querybuf,querylen);
	argv = sdssplitargs(aux,&argc);
	sdsfree(aux);
	if (argv == NULL) {
		// TODO
#if 0
		addReplyError(c,"Protocol error: unbalanced quotes in request");
		setProtocolError(c,0);
		return SERVER_ERR;
#endif
		return SERVER_ERR;
	}


	/* Leave data after the first line of the query in the buffer */
	sdsrange(c->querybuf,querylen+2,-1);

	/* Setup argv array on client structure */
	if (argc) {
		if (c->argv) zfree(c->argv);
		c->argv = zmalloc(sizeof(sds)*argc);
	}

	/* Create redis objects for all arguments. */
	for (c->argc = 0, j = 0; j < argc; j++) {
		if (sdslen(argv[j])) {
			c->argv[c->argc] = sdsdup(argv[j]);
			c->argc++;
		} else {
			sdsfree(argv[j]);
		}
	}
	zfree(argv);
	return SERVER_OK;
}

int processMultibulkBuffer(client *c)
{
	char *newline = NULL;
	int pos = 0, ok;
	long long ll;

	if (c->multibulklen == 0) {
		/* The client should have been reset */
		assert(c->argc == 0);

		/* Multi bulk length cannot be read without a \r\n */
		newline = strchr(c->querybuf,'\r');
		if (newline == NULL) {
			if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
			// TODO
#if 0
				addReplyError(c,"Protocol error: too big mbulk count string");
				setProtocolError(c,0);
#endif
			}
			return SERVER_ERR;
		}

		/* Buffer should also contain \n */
		if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
			return SERVER_ERR;

		/* We know for sure there is a whole line since newline != NULL,
		 * so go ahead and find out the multi bulk length. */
		assert(c->querybuf[0] == '*');
		ok = string2ll(c->querybuf+1,newline-(c->querybuf+1),&ll);
		if (!ok || ll > 1024*1024) {
			server_log(LL_WARNING, "Protocol error: invalid multibulk length");
			return SERVER_ERR;
		}

		pos = (newline-c->querybuf)+2;
		if (ll <= 0) {
			sdsrange(c->querybuf,pos,-1);
			return SERVER_OK;
		}

		c->multibulklen = ll;

		/* Setup argv array on client structure */
		if (c->argv) zfree(c->argv);
		c->argv = zmalloc(sizeof(sds)*c->multibulklen);
	}

	assert(c->multibulklen > 0);
	while(c->multibulklen) {
		/* Read bulk length if unknown */
		if (c->bulklen == -1) {
			newline = strchr(c->querybuf+pos,'\r');
			if (newline == NULL) {
				if (sdslen(c->querybuf) > PROTO_INLINE_MAX_SIZE) {
					server_log(LL_WARNING, "Protocol error: too big bulk count string");
					return SERVER_ERR;
				}
				break;
			}

			/* Buffer should also contain \n */
			if (newline-(c->querybuf) > ((signed)sdslen(c->querybuf)-2))
				break;

			if (c->querybuf[pos] != '$') {
				server_log(LL_WARNING, 
						"Protocol error: Protocol error: expected '$', got '%c'",
						c->querybuf[pos]);
				return SERVER_ERR;
			}

			ok = string2ll(c->querybuf+pos+1,newline-(c->querybuf+pos+1),&ll);
			if (!ok || ll < 0 || ll > 512*1024*1024) {
				server_log(LL_WARNING, "Protocol error: invalid bulk length");
				return SERVER_ERR;
			}

			pos += newline-(c->querybuf+pos)+2;
			if (ll >= PROTO_MBULK_BIG_ARG) {
				size_t qblen;

				/* If we are going to read a large object from network
				 * try to make it likely that it will start at c->querybuf
				 * boundary so that we can optimize object creation
				 * avoiding a large copy of data. */
				sdsrange(c->querybuf,pos,-1);
				pos = 0;
				qblen = sdslen(c->querybuf);
				/* Hint the sds library about the amount of bytes this string is
				 * going to contain. */
				if (qblen < (size_t)ll+2)
					c->querybuf = sdsMakeRoomFor(c->querybuf,ll+2-qblen);
			}
			c->bulklen = ll;
		}

		/* Read bulk argument */
		if (sdslen(c->querybuf)-pos < (unsigned)(c->bulklen+2)) {
			/* Not enough data (+2 == trailing \r\n) */
			break;
		} else {
			/* Optimization: if the buffer contains JUST our bulk element
			 * instead of creating a new object by *copying* the sds we
			 * just use the current sds string. */
			if (pos == 0 &&
				c->bulklen >= PROTO_MBULK_BIG_ARG &&
				(signed) sdslen(c->querybuf) == c->bulklen+2)
			{
				c->argv[c->argc++] = sdsdup(c->querybuf);
				sdsIncrLen(c->querybuf,-2); /* remove CRLF */
				/* Assume that if we saw a fat argument we'll see another one
				 * likely... */
				c->querybuf = sdsnewlen(NULL,c->bulklen+2);
				sdsclear(c->querybuf);
				pos = 0;
			} else {
				c->argv[c->argc++] = sdsnewlen(c->querybuf+pos, c->bulklen);
				//	createStringObject(c->querybuf+pos,c->bulklen);
				pos += c->bulklen+2;
			}
			c->bulklen = -1;
			c->multibulklen--;
		}
	}

	/* Trim to pos */
	if (pos) sdsrange(c->querybuf,pos,-1);

	/* We're done when c->multibulk == 0 */
	if (c->multibulklen == 0) return SERVER_OK;

	/* Still not read to process the command */
	return SERVER_ERR;
}

void resetClient(client *c)
{
	int j;

	for (j = 0; j < c->argc; j++) {
		sdsfree(c->argv[j]);
	}
	c->argc = 0;
	c->cmd = NULL;
	c->reqtype = 0;
	c->multibulklen = 0;
	c->bulklen = -1;
	return;
}

static void processInputBuffer(client *c)
{
	/* Keep processing while there is something in the input buffer */
	while(sdslen(c->querybuf)) {
		/* Determine request type when unknown. */
		if (!c->reqtype) {
			if (c->querybuf[0] == '*') {
				c->reqtype = PROTO_REQ_MULTIBULK;
			} else {
				c->reqtype = PROTO_REQ_INLINE;
			}
		}

		if (c->reqtype == PROTO_REQ_INLINE) {
			if (processInlineBuffer(c) != SERVER_OK) break;
		} else if (c->reqtype == PROTO_REQ_MULTIBULK) {
			if (processMultibulkBuffer(c) != SERVER_OK) break;
		} else {
			server_panic("Unknown request type");
		}

		/* Multibulk processing could see a <= 0 length. */
		if (c->argc == 0) {
			resetClient(c);
		} else {
			/* Only reset the client when the command was executed. */
			if (processCommand(c) == SERVER_OK) {
				resetClient(c);
			}
		}
	}
	return;
}


static void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
	client *c = (client*) privdata;
	int nread, readlen;
	size_t qblen;
	UNUSED(el);
	UNUSED(mask);

	readlen = PROTO_IOBUF_LEN;
	/* If this is a multi bulk request, and we are processing a bulk reply
	 * that is large enough, try to maximize the probability that the query
	 * buffer contains exactly the SDS string representing the object, even
	 * at the risk of requiring more read(2) calls. This way the function
	 * processMultiBulkBuffer() can avoid copying buffers to create the
	 * Redis Object representing the argument. */
	if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
		&& c->bulklen >= PROTO_MBULK_BIG_ARG)
	{
		int remaining = (unsigned)(c->bulklen+2)-sdslen(c->querybuf);

		if (remaining < readlen) readlen = remaining;
	}

	qblen = sdslen(c->querybuf);
	if (c->querybuf_peak < qblen) c->querybuf_peak = qblen;
	c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
	nread = read(fd, c->querybuf+qblen, readlen);
	if (nread == -1) {
		if (errno == EAGAIN) {
			return;
		} else {
			server_log(LL_VERBOSE, "Reading from client: %s",strerror(errno));
			freeClient(c);
			return;
		}
	} else if (nread == 0) {
		server_log(LL_VERBOSE, "Client closed connection");
		freeClient(c);
		return;
	}

	sdsIncrLen(c->querybuf,nread);
	processInputBuffer(c);

	return;
}



client *createClient(int fd)
{
	client *c = zmalloc(sizeof(client));

	/* passing -1 as fd it is possible to create a non connected client.
	 * This is useful since all the commands needs to be executed
	 * in the context of a client. When commands are executed in other
	 * contexts (for instance a Lua script) we need a non connected client. */
	if (fd != -1) {
		anetNonBlock(NULL, fd);
		anetEnableTcpNoDelay(NULL, fd);
		anetKeepAlive(NULL, fd, SERVER_KEEPALIVE_INTERVAL);

		if (aeCreateFileEvent(server.el, fd, AE_READABLE,
					readQueryFromClient, c) == AE_ERR) {
			close(fd);
			zfree(c);
			return NULL;
		}
	}

	c->fd = fd;
	c->bufpos = 0;
	c->querybuf = sdsempty();
	c->querybuf_peak = 0;
	c->reqtype = 0;
	c->argc = 0;
	c->argv = NULL;
	c->flags = 0;
	c->bulklen = -1;
	c->multibulklen = 0;
	return c;
}


static void acceptCommonHandler(int fd, int flags, char *ip)
{
	client *c = createClient(fd);
	if (c == NULL) {
		server_log(LL_WARNING,
			"Error registering fd event for the new client: %s (fd=%d)",
			strerror(errno), fd);
		close(fd);
	}

	return;
}


#define NET_IP_STR_LEN 46 /*  INET6_ADDRSTRLEN is 46, but we need to be sure */
static void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask)
{

	int cfd, cport;
	char cip[NET_IP_STR_LEN];
	UNUSED(el);
	UNUSED(mask);
	UNUSED(privdata);

	cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
	if (cfd == ANET_ERR) {
		if (errno != EWOULDBLOCK) {
			/* ERROR LOG */
			return;
		}
	}

	server_log(LL_VERBOSE,"Accepted %s:%d", cip, cport);
	acceptCommonHandler(cfd, 0, cip);
	return;
}

void loadDb()
{
	/* check file existence */
	if (access(server.db_filename, F_OK) == -1) {
		return;
	}

	FILE *fp = fopen(server.db_filename, "r");
	assert(fp != NULL);
	size_t len = 0;
	ssize_t nread;
	char *line = NULL;
	sds *kvs;
	int count;

	while ((nread = getline(&line, &len, fp) != -1)) {
		sds tmp = sdsnewlen(line, len - 2);
		kvs = sdssplitlen(tmp, sdslen(tmp), " ", 1, &count);
		if (count != 2) {
			server_log(LL_WARNING, "Data file format error, load failed.");
			exit(1);
		}
		
		/* just add raw key to dict */
		dictAddRaw(server.dict, sdsdup(kvs[0]));	
		/* insert into skiplist */
		insert_skiplist(server.sl, kvs[0], kvs[1]);

		sdsfreesplitres(kvs, count);
		sdsfree(tmp);
	}


	free(line);
	fclose(fp);
	return;
}


void initDb()
{
	/* signal handle */
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	setupSignalHandlers();

    /* set random seed */
    srand((unsigned)time(NULL));	

	server.pid = getpid();
	server.el = aeCreateEventLoop(MAX_EVENT_SIZE);

	/* init commands */
	populateCommandTable();

	/* create socket server and listen */
	if ((server.sock_fd = listenToPort(server.port)) == SERVER_ERR) {
		server_log(LL_WARNING, "Listen to port %d error", server.port);
		exit(1);
	}

	/* create skiplist and hashmap */
	server.dict = dictCreate(&slDictType, NULL);
	server.sl = create_skiplist();
	
	/* load data from data file */
	loadDb();

	/* create file event to handle connection request */
	if (aeCreateFileEvent(server.el, server.sock_fd, AE_READABLE,
							acceptTcpHandler, NULL) == AE_ERR) {
		server_panic("Unrecoverable error creating file event.");
	}

#if 0
	bioInit();
#endif

	return;
}

void saveDb()
{
	char tmpfile[256];
	snprintf(tmpfile,256,"temp-%d.data", (int) getpid());

	FILE *fp = fopen(tmpfile, "w");
	assert(fp != NULL);

	sl_node *node = server.sl->head->next[0];
	while (node) {
		fprintf(fp, "%s %s\n", node->key, node->val);
		node = node->next[0];
	}

	fclose(fp);
	rename(tmpfile, server.db_filename);
	return;
}

int main(int argc, char *argv[])
{
	/* analyse input parameters */
	process_para(argc, argv);
	/* set env */
	spt_init(argc, argv);
	/* init global server structure */
	init_config();

	server.config_file = getAbsolutePath(argv[2]);
	/* load configure from config file */
	load_config();
	if (server.daemonize) daemonize();

	initDb();
	if (server.daemonize || server.pidfile) {
		createPidFile();
	}
	setproctitle("%s *:%d", argv[0], server.port);

	atexit(saveDb);

#if 0
	/* execute every 100ms */
	if (aeCreateTimeEvent(server.el, 100, serverCron,
				NULL, NULL) == AE_ERR) {
		server_panic("Unrecoverable error creating time event.");
	}
#endif

	aeMain(server.el);
	aeDeleteEventLoop(server.el);

	return 0;
}
