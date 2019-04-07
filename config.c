#include "db.h"
#include "sds.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

void loadServerConfigFromString(char *config)
{
	char *err = NULL;
	int linenum = 0, totlines, i;
	sds *lines;

	lines = sdssplitlen(config, strlen(config),"\n",1,&totlines);

	for (i = 0; i < totlines; i++) {
		sds *argv;
		int argc;

		linenum = i+1;
		lines[i] = sdstrim(lines[i]," \t\r\n");

		/* Skip comments and blank lines */
		if (lines[i][0] == '#' || lines[i][0] == '\0') continue;

		/* Split into arguments */
		argv = sdssplitargs(lines[i],&argc);
		if (argv == NULL) {
			err = "Unbalanced quotes in configuration line";
			goto loaderr;
		}

		/* Skip this line if the resulting command vector is empty. */
		if (argc == 0) {
			sdsfreesplitres(argv,argc);
			continue;
		}
		sdstolower(argv[0]);

		/* Execute config directives */
		if (!strcasecmp(argv[0],"port") && argc == 2) {
			server.port = atoi(argv[1]);
			if (server.port < 0 || server.port > 65535) {
				err = "Invalid port"; goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"loglevel") && argc == 2) {
			if (!strcasecmp(argv[1],"debug")) {
				server.verbosity = LL_DEBUG;
			} else if (!strcasecmp(argv[1],"verbose")) {
				server.verbosity = LL_VERBOSE;
			} else if (!strcasecmp(argv[1],"notice")) {
				server.verbosity = LL_NOTICE;
			} else if (!strcasecmp(argv[1],"warning")) {
				server.verbosity = LL_WARNING;
			} else {
				err = "Invalid log level. Must be one of debug, notice, warning";
				goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"dir") && argc == 2) {
			if (chdir(argv[1]) == -1) {
				err = "Changing directory failed";
				goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"logfile") && argc == 2) {
			FILE *logfp;

			zfree(server.log_file);
			server.log_file = zstrdup(argv[1]);
			if (server.log_file[0] != '\0') {
				/* Test if we are able to open the file. The server will not
				 * be able to abort just for this problem later... */
				logfp = fopen(server.log_file,"a");
				if (logfp == NULL) {
					err = sdscatprintf(sdsempty(),
						"Can't open the log file: %s", strerror(errno));
					goto loaderr;
				}
				fclose(logfp);
			}
		} else if (!strcasecmp(argv[0],"daemonize") && argc == 2) {
			if ((server.daemonize = yesnotoi(argv[1])) == -1) {
				err = "argument must be 'yes' or 'no'"; goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"pidfile") && argc == 2) {
			zfree(server.pidfile);
			server.pidfile = zstrdup(argv[1]);
		} else if (!strcasecmp(argv[0],"fixed-length") && argc == 3) {
			server.fl = (struct fixed_length *)malloc(sizeof(struct fixed_length));
			server.fl->key_len = atoi(argv[1]);
			server.fl->val_len = atoi(argv[2]);
        } else if (!strcasecmp(argv[0],"dbfilename") && argc == 2) {
            if (!pathIsBaseName(argv[1])) {
                err = "dbfilename can't be a path, just a filename";
                goto loaderr;
            }
            zfree(server.db_filename);
            server.db_filename = zstrdup(argv[1]);
		} else {
			err = "Bad directive or wrong number of arguments"; goto loaderr;
		}
		sdsfreesplitres(argv,argc);
	}

	sdsfreesplitres(lines,totlines);
	return;

loaderr:
	fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
	fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
	fprintf(stderr, ">>> '%s'\n", lines[i]);
	fprintf(stderr, "%s\n", err);
	exit(1);
}
