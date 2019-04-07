#ifndef _LOG_H_                                                             
#define _LOG_H_

/* Log levels */
#define LL_DEBUG 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define LL_RAW (1<<10) /* Modifier to log without timestamp */
#define CONFIG_DEFAULT_VERBOSITY LL_NOTICE

#define server_panic(_e) _serverPanic(#_e,__FILE__,__LINE__),_exit(1)

void server_log(int level, const char *fmt, ...);

#endif
