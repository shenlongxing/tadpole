#include "db.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#define LOG_MAX_LEN    1024 /*  Default maximum length of syslog message */

/* Low level logging. To use only for very big messages, otherwise
 * server_log() is to prefer. */
static void server_logRaw(int level, const char *msg)
{
    const char *c = ".-*#";
    FILE *fp; 
    char buf[64];
    int rawmode = (level & LL_RAW);
    int log_to_stdout = server.log_file[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.log_file,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off; 
        struct timeval tv;

        gettimeofday(&tv, NULL);
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"%d:%s %c %s\n",
            (int)getpid(), buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);

	return;
}


void server_log(int level, const char *fmt, ...)
{
	va_list ap;
	char msg[LOG_MAX_LEN];
	
	if ((level&0xff) < server.verbosity) return;
	
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	
	server_logRaw(level,msg);
	return;
}

void _serverPanic(char *msg, char *file, int line)
{
    server_log(LL_WARNING,"------------------------------------------------");
    server_log(LL_WARNING,"!!! Software Failure. Press left mouse button to continue");
    server_log(LL_WARNING,"Guru Meditation: %s #%s:%d",msg,file,line);
    server_log(LL_WARNING,"------------------------------------------------");
    *((char*)-1) = 'x';
}
