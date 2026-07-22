#ifndef _QUEUE_H_
#define _QUEUE_H_

typedef struct Queue Queue;

Queue *queue_create(int capacity);
void   queue_push(Queue *q, const char *path);
char  *queue_pop(Queue *q);
void   queue_shutdown(Queue *q);
void   queue_destroy(Queue *q);

#endif  // _QUEUE_H_
