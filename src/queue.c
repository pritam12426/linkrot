#include "queue.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>

#include "log.h"

struct Queue {
	char         **buf;
	int            capacity;
	int            head;
	int            tail;
	int            count;
	bool           shutdown;
	pthread_mutex_t mutex;
	pthread_cond_t  not_empty;
	pthread_cond_t  not_full;
};

Queue *queue_create(int capacity)
{
	LOG_TRACE("allocating queue (capacity=%d)", capacity);

	Queue *q = calloc(1, sizeof(Queue));
	if (!q) return NULL;

	q->buf = calloc((size_t)capacity, sizeof(char *));
	if (!q->buf) { free(q); return NULL; }

	q->capacity = capacity;
	q->head     = 0;
	q->tail     = 0;
	q->count    = 0;
	q->shutdown = false;
	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->not_empty, NULL);
	pthread_cond_init(&q->not_full, NULL);

	LOG_DEBUG("queue initialized: capacity=%d head=%d tail=%d count=%d",
	          q->capacity, q->head, q->tail, q->count);
	return q;
}

void queue_push(Queue *q, const char *path)
{
	pthread_mutex_lock(&q->mutex);

	while (q->count == q->capacity && !q->shutdown) {
		LOG_TRACE("queue full (count=%d/%d), blocking push", q->count, q->capacity);
		pthread_cond_wait(&q->not_full, &q->mutex);
	}

	if (q->shutdown) {
		LOG_TRACE("queue shutdown, dropping push for '%s'", path);
		pthread_mutex_unlock(&q->mutex);
		return;
	}

	q->buf[q->tail] = (char *)path;
	q->tail = (q->tail + 1) % q->capacity;
	q->count++;

	LOG_TRACE("queue push: '%s' (count=%d/%d)", path, q->count, q->capacity);
	pthread_cond_signal(&q->not_empty);
	pthread_mutex_unlock(&q->mutex);
}

char *queue_pop(Queue *q)
{
	pthread_mutex_lock(&q->mutex);

	while (q->count == 0 && !q->shutdown) {
		LOG_TRACE("queue empty (count=0), blocking pop");
		pthread_cond_wait(&q->not_empty, &q->mutex);
	}

	if (q->count == 0) {
		LOG_TRACE("queue empty + shutdown, returning NULL");
		pthread_mutex_unlock(&q->mutex);
		return NULL;
	}

	char *path = q->buf[q->head];
	q->head = (q->head + 1) % q->capacity;
	q->count--;

	LOG_TRACE("queue pop: '%s' (count=%d/%d)", path, q->count, q->capacity);
	pthread_cond_signal(&q->not_full);
	pthread_mutex_unlock(&q->mutex);
	return path;
}

void queue_shutdown(Queue *q)
{
	LOG_DEBUG("queue shutdown signaled");
	pthread_mutex_lock(&q->mutex);
	q->shutdown = true;
	pthread_cond_broadcast(&q->not_empty);
	pthread_cond_broadcast(&q->not_full);
	pthread_mutex_unlock(&q->mutex);
}

void queue_destroy(Queue *q)
{
	if (!q) return;
	LOG_TRACE("destroying queue");
	pthread_mutex_destroy(&q->mutex);
	pthread_cond_destroy(&q->not_empty);
	pthread_cond_destroy(&q->not_full);
	free(q->buf);
	free(q);
	LOG_DEBUG("queue destroyed");
}
