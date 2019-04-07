#ifndef _COMMAND_H_
#define _COMMAND_H_ 

#include "sds.h"

void populateCommandTable();
struct server_command* lookupCommand(sds name);
unsigned int dictSdsCaseHash(const void *key);
int dictSdsKeyCaseCompare(void *privdata, const void *key1, const void *key2);
void dictSdsDestructor(void *privdata, void *val);
#endif
