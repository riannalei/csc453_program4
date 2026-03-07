/*
 * queue.c - A generic FIFO queue (linked list implementation)
 *
 * Provided for the bfind assignment. Do not modify this file.
 */

#include "queue.h"

#include <stdlib.h>

void queue_init(queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

int queue_enqueue(queue_t *q, void *data) {
    queue_node_t *node = malloc(sizeof(queue_node_t));
    if (!node) {
        return -1;
    }
    node->data = data;
    node->next = NULL;

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->size++;
    return 0;
}

void *queue_dequeue(queue_t *q) {
    if (!q->head) {
        return NULL;
    }

    queue_node_t *node = q->head;
    void *data = node->data;

    q->head = node->next;
    if (!q->head) {
        q->tail = NULL;
    }
    q->size--;

    free(node);
    return data;
}

bool queue_is_empty(const queue_t *q) {
    return q->size == 0;
}

size_t queue_size(const queue_t *q) {
    return q->size;
}

void queue_destroy(queue_t *q) {
    queue_node_t *cur = q->head;
    while (cur) {
        queue_node_t *next = cur->next;
        free(cur);
        cur = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}
