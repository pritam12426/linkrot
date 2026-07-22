#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "log.h"
#include "project_config.h"
#include "queue.h"
#include "traverse.h"
#include "worker.h"

const char *argp_program_version     = MAIN_BINARY " " PROJECT_VERSION;
const char *argp_program_bug_address = PROJECT_HOMEPAGE_URL "/issues" "\n" AUTH_MESSAGE;

int main(int argc, char **argv)
{
	LOG_TRACE("parsing command-line arguments");
	Config *cfg = config_parse(argc, argv);
	log_init(cfg->log_level);

	if (LOG_LEVEL_IS_ENABLED(LOG_LEVEL_DEBUG)) {
		LOG_CUSTOM(LOG_LEVEL_DEBUG, false, "Command-line args: [");
		for (int i = 0; i < argc; i++) {
			fprintf(stderr, "\"%s\"", argv[i]);
			if (i != argc - 1) fputs(", ", stderr);
		}
		fputs("]\n", stderr);
	}

	LOG_DEBUG("config: dir=%s glob=%s depth=%d workers=%d delete=%d dry_run=%d print=%d delete_cmd=%s",
	          cfg->dir, cfg->glob, cfg->max_depth, cfg->num_workers,
	          cfg->delete, cfg->dry_run, cfg->print,
	          cfg->delete_cmd ? cfg->delete_cmd : "(null)");

	LOG_TRACE("creating bounded queue (capacity=1024)");
	Queue *queue = queue_create(1024);
	if (!queue) {
		LOG_FATAL("Failed to create queue");
		return 1;
	}
	LOG_DEBUG("queue created");

	LOG_TRACE("starting worker pool (%d workers)", cfg->num_workers);
	worker_pool_start(queue, cfg, cfg->num_workers);
	LOG_DEBUG("worker pool started, launching traversal");

	traverse(cfg, queue);
	LOG_DEBUG("traversal complete, shutting down queue");
	queue_shutdown(queue);

	LOG_TRACE("waiting for workers to finish");
	WorkerStats stats;
	worker_pool_wait(&stats);

	LOG_DEBUG("workers joined: found=%ld deleted=%ld errors=%ld",
	          stats.found, stats.deleted, stats.errors);

	if (cfg->delete) {
		LOG_INFO("Found %ld broken symlink(s), deleted %ld, %ld error(s).",
		         stats.found, stats.deleted, stats.errors);
	} else {
		LOG_INFO("Found %ld broken symlink(s).", stats.found);
	}

	LOG_TRACE("destroying queue");
	queue_destroy(queue);
	LOG_TRACE("exiting with code %d", (stats.errors > 0) ? 1 : 0);
	return (stats.errors > 0) ? 1 : 0;
}
