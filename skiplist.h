#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__
#include "sds.h"


typedef struct skiplist_node {
	char *key;
	char *val;
	/* flexible array to store level nodes */
	struct skiplist_node *next[0];
}sl_node;

typedef struct skiplist {
	int level;
	int length;
	struct skiplist_node *head, *tail;
}skiplist;


skiplist *create_skiplist();
sds search_skiplist(skiplist *sl, sds key);
int insert_skiplist(skiplist *sl, sds key, sds val);
int delete_skiplist(skiplist *sl, sds key);
sds find_max_skiplist(skiplist *sl);

#endif
