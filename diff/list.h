// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2021 LeavaTail
 */
#ifndef _LIST_H
#define _LIST_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct node {
	uint64_t index;
	struct node *next;
} node_t;

static inline node_t *last_node(node_t *node)
{
	while (node != NULL && node->next != NULL)
		node = node->next;
	return node;
}

static inline void insert_node(node_t *head, uint64_t i)
{
	node_t *node;

	if (!head) {
		head = malloc(sizeof(node_t));
		head->index = i;
		head->next = NULL;
	} else {
		node = malloc(sizeof(node_t));
		node->index = i;
		node->next = head->next;
		head->next = node;
	}
}

static inline void append_node(node_t *head, uint64_t i)
{
	insert_node(last_node(head), i);
}

static inline void delete_node(node_t *node)
{
	node_t *tmp;

	if (!node)
		return;

	if ((tmp = node->next) != NULL) {
		node->next = tmp->next;
		free(tmp);
	}
	free(node);
}

static inline node_t *search_node(node_t *node, uint64_t i)
{
	while (node != NULL && node->next != NULL) {
		node = node->next;
		if (i == node->index)
			return node;
	}
	return NULL;
}

static inline void print_node(node_t *node)
{
	while (node != NULL && node->next != NULL) {
		node = node->next;
		fprintf(stdout, "%lu -> ", node->index);
	}
	fprintf(stdout, "NULL\n");
}

#endif /*_LIST_H */
