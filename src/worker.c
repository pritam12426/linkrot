#include "worker.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "config.h"
#include "log.h"
#include "queue.h"
#include "symlink.h"

static Queue         *w_queue;
static const Config  *w_cfg;
static int            w_count;
static pthread_t     *w_threads;
static WorkerStats   *w_stats;

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker_fn(void *arg)
{
	int id = *(int *)arg;
	free(arg);

	LOG_DEBUG("worker %d started", id);

	WorkerStats stats = { .found = 0, .deleted = 0, .errors = 0 };
	char target_buf[PATH_MAX];

	for (;;) {
		LOG_TRACE("worker %d: waiting for path from queue", id);
		char *path = queue_pop(w_queue);
		if (!path) {
			LOG_DEBUG("worker %d: received shutdown signal, exiting", id);
			break;
		}

		LOG_TRACE("worker %d: processing '%s'", id, path);

		if (!symlink_matches_glob(path, w_cfg->glob)) {
			LOG_TRACE("worker %d: skipping '%s' — glob mismatch (pattern='%s')", id, path, w_cfg->glob);
			free(path);
			continue;
		}
		LOG_TRACE("worker %d: glob matched '%s'", id, path);

		if (!symlink_is_broken(path, target_buf, sizeof(target_buf))) {
			LOG_TRACE("worker %d: skipping '%s' — target exists", id, path);
			free(path);
			continue;
		}

		stats.found++;
		LOG_DEBUG("worker %d: broken symlink found: '%s' -> '%s'", id, path, target_buf);

		if (w_cfg->print) {
			LOG_TRACE("worker %d: printing '%s'", id, path);
			pthread_mutex_lock(&output_mutex);
			if (w_cfg->null) {
				fputs(path, stdout);
				fputc('\0', stdout);
			} else {
				puts(path);
			}
			pthread_mutex_unlock(&output_mutex);
		} else if (w_cfg->dry_run) {
			LOG_INFO("[dry-run] %s -> %s", path, target_buf);
		} else if (w_cfg->delete) {
			LOG_DEBUG("worker %d: deleting '%s' (cmd=%s)", id, path, w_cfg->delete_cmd ? w_cfg->delete_cmd : "unlink");
			if (symlink_delete(path, w_cfg->delete_cmd) != 0) {
				stats.errors++;
				LOG_DEBUG("worker %d: delete FAILED for '%s'", id, path);
			} else {
				stats.deleted++;
				LOG_DEBUG("worker %d: delete OK for '%s'", id, path);
			}
		} else {
			LOG_INFO("%s -> %s", path, target_buf);
		}

		free(path);
	}

	LOG_DEBUG("worker %d finished: found=%ld deleted=%ld errors=%ld",
	          id, stats.found, stats.deleted, stats.errors);
	w_stats[id] = stats;
	return NULL;
}

void worker_pool_start(Queue *queue, const Config *cfg, int count)
{
	LOG_DEBUG("initializing worker pool: count=%d", count);

	w_queue  = queue;
	w_cfg    = cfg;
	w_count  = count;
	w_threads = calloc((size_t)count, sizeof(pthread_t));
	w_stats   = calloc((size_t)count, sizeof(WorkerStats));

	for (int i = 0; i < count; i++) {
		int *id = malloc(sizeof(int));
		*id = i;
		LOG_TRACE("spawning worker %d", i);
		pthread_create(&w_threads[i], NULL, worker_fn, id);
	}

	LOG_DEBUG("all %d workers spawned", count);
}

void worker_pool_wait(WorkerStats *out)
{
	LOG_TRACE("joining all %d workers", w_count);

	for (int i = 0; i < w_count; i++)
		pthread_join(w_threads[i], NULL);

	LOG_DEBUG("all workers joined, merging stats");

	out->found   = 0;
	out->deleted = 0;
	out->errors  = 0;
	for (int i = 0; i < w_count; i++) {
		LOG_TRACE("worker %d stats: found=%ld deleted=%ld errors=%ld",
		          i, w_stats[i].found, w_stats[i].deleted, w_stats[i].errors);
		out->found   += w_stats[i].found;
		out->deleted += w_stats[i].deleted;
		out->errors  += w_stats[i].errors;
	}

	LOG_DEBUG("merged stats: found=%ld deleted=%ld errors=%ld",
	          out->found, out->deleted, out->errors);

	free(w_threads);
	free(w_stats);
	w_threads = NULL;
	w_stats   = NULL;
}
