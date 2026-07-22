#ifndef _WORKER_H_
#define _WORKER_H_

#include "config.h"
#include "queue.h"

typedef struct {
	long found;
	long deleted;
	long errors;
} WorkerStats;

void worker_pool_start(Queue *queue, const Config *cfg, int count);
void worker_pool_wait(WorkerStats *out);

#endif  // _WORKER_H_
