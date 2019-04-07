#include "skiplist.h"
#include "sds.h"

#include <stdlib.h>
#include <string.h>

#define MAX_LEVEL 16

sl_node *create_skiplist_node(int level, sds key, sds val)
{
	/* flexible array to index level */
	sl_node *node = (sl_node *)malloc(sizeof(sl_node) + level * sizeof(void *));
	if (!node) {
		return NULL;
	}

	node->key = key;
	node->val = val;
	return node;
}


skiplist *create_skiplist()
{
	skiplist *sl = (skiplist *)malloc(sizeof(skiplist));
	if (!sl) {
		return NULL;
	}

	sl->level = 1;
	sl->length = 0;
	sl->head = create_skiplist_node(MAX_LEVEL, NULL, NULL);
	int i;
	for (i = 0; i < MAX_LEVEL; i++) {
		sl->head->next[i] = 0;
	}

	return sl;
}

static int gen_random_level()
{
	int level = 1;
	while (rand() % 2) {
		level++;
	}

	return (level > MAX_LEVEL) ? MAX_LEVEL : level;
}

int slKeyCompare(sds key1, sds key2)
{
    int l1,l2;

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1);
}


int insert_skiplist(skiplist *sl, sds key, sds val)
{
	sl_node *update[MAX_LEVEL];
	sl_node *x = sl->head, *q = NULL;
	int i;

	/* search from high to low to find target level */
	for (i = sl->level - 1; i >= 0; i--) {
		while ((q = x->next[i]) && (slKeyCompare(q->key, key) < 0)) {
			x = q;
		}
		update[i] = x;
	}

	if (q && slKeyCompare(q->key, key) == 0) {
		sdsfree(q->val);
		q->val = val;
		sl->length++;
		return 0;
	}

	/* generate a random level */
	int target_level = gen_random_level();
	if (target_level > sl->level) {
		for (i = sl->level; i < target_level; i++) {
			update[i] = sl->head;
		}
		sl->level = target_level;
	}

	q = create_skiplist_node(target_level, sdsdup(key), sdsdup(val));
	if (!q) {
		return -1;
	}

	/* update current list */
	for (i = target_level - 1; i >= 0; i--) {
		q->next[i] = update[i]->next[i];
		update[i]->next[i] = q;
	}

	sl->length++;
	return 0;
}

static void free_skiplist_node(sl_node *node)
{
	if (!node) {
		return;
	}

	sdsfree(node->key);
	sdsfree(node->val);
	free(node);

	return;
}

int delete_skiplist(skiplist *sl, sds key)
{
	sl_node *update[MAX_LEVEL];
	sl_node *q = NULL, *p = sl->head;
	int i;

	for (i = sl->level - 1; i >= 0; i--) {
		while ((q = p->next[i]) && slKeyCompare(q->key, key) < 0) {
			p = q;
		}
		update[i] = p;
	}

	if (!q|| slKeyCompare(q->key, key) != 0) {
		return -1;
	}

	for (i = sl->level - 1; i >= 0; i--) {
		if (update[i]->next[i] == q) {
			update[i]->next[i] = q->next[i];
			if (sl->head->next[i] == NULL) {
				sl->level--;
			}
		}
	}

	free_skiplist_node(q);
	sl->length--;
	return 0;
}

sds search_skiplist(skiplist *sl, sds key)
{
    sl_node *q = NULL, *p=sl->head;
    int i;
    for (i = sl->level - 1; i >= 0; i--) {
        while ((q = p->next[i]) && slKeyCompare(q->key, key) < 0) {
            p = q;
        }

        if (q && slKeyCompare(key, q->key) == 0)
            return q->val;
    }   
    return NULL;
}

int replace_skiplist(skiplist *sl, sds key, sds newVal)
{
    sl_node *q = NULL, *p=sl->head;
    int i;
    for(i = sl->level - 1; i >= 0; i--) {
        while((q = p->next[i]) && slKeyCompare(q->key, key) < 0) {
            p = q;
        }

        if (q && slKeyCompare(key, q->key) == 0) {
            sdsfree(q->val);
			q->val = sdsdup(newVal);
			return 0;
		}
    }
    return -1;
}

sds find_max_skiplist(skiplist *sl)
{
	int i = sl->level- 1;
	sl_node *node = sl->head->next[i];

#if 0
	for (; i >= 0; i--) {
		while (node->next[i]) {
			node = node->next[i];
		}
		node = node->next[i-1];
	}
#else
	while (node->next[0]) {
		node = node->next[0];
	}

#endif

	return node->key;
}
