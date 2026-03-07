/*
 * queue.h - A generic FIFO queue (linked list implementation)
 *
 * Provided for the bfind assignment. You may use this queue as-is.
 * Do not modify this file.
 */

#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdlib.h>

/* A single node in the queue */
typedef struct queue_node {
    void *data;
    struct queue_node *next;
} queue_node_t;

/* The queue itself */
typedef struct {
    queue_node_t *head;
    queue_node_t *tail;
    size_t size;
} queue_t;

/* Initialize a queue. Must be called before use. */
void queue_init(queue_t *q);

/* Add an element to the back of the queue. Returns 0 on success, -1 on
 * allocation failure. The queue takes ownership of the pointer but does
 * NOT copy the data — the caller is responsible for ensuring the pointer
 * remains valid until it is dequeued. */
int queue_enqueue(queue_t *q, void *data);

/* Remove and return the element at the front of the queue.
 * Returns NULL if the queue is empty. */
void *queue_dequeue(queue_t *q);

/* Return true if the queue is empty. */
bool queue_is_empty(const queue_t *q);

/* Return the number of elements in the queue. */
size_t queue_size(const queue_t *q);

/* Free all nodes in the queue. Does NOT free the data pointers —
 * the caller must dequeue and free each element first, or accept
 * the leak. After this call the queue is empty and may be reused. */
void queue_destroy(queue_t *q);

#endif /* QUEUE_H */
