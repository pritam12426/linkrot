#define _XOPEN_SOURCE 700

#include "traverse.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "queue.h"

static const Config *t_cfg;
static Queue        *t_queue;

static int walk_callback(const char *fpath, const struct stat *sb,
                         int typeflag, struct FTW *ftwbuf)
{
	(void)sb;

	LOG_TRACE("nftw visit: '%s' level=%d typeflag=%d", fpath, ftwbuf->level, typeflag);

	if (t_cfg->max_depth >= 0 && ftwbuf->level > t_cfg->max_depth) {
		LOG_TRACE("skipping '%s': depth %d exceeds max %d", fpath, ftwbuf->level, t_cfg->max_depth);
		return 0;
	}

	if (typeflag != FTW_SL) {
		LOG_TRACE("skipping '%s': not a symlink (typeflag=%d)", fpath, typeflag);
		return 0;
	}

	LOG_TRACE("found symlink: '%s' (depth=%d)", fpath, ftwbuf->level);

	char *path_copy = strdup(fpath);
	if (!path_copy) {
		LOG_ERROR("Out of memory");
		return -1;
	}

	queue_push(t_queue, path_copy);
	LOG_TRACE("enqueued symlink: '%s'", fpath);
	return 0;
}

int traverse(const Config *cfg, Queue *queue)
{
	t_cfg   = cfg;
	t_queue = queue;

	LOG_DEBUG("starting nftw walk on '%s' (depth=%d)", cfg->dir, cfg->max_depth);

	int ret = nftw(cfg->dir, walk_callback, 64, FTW_PHYS);
	if (ret == -1) {
		LOG_PERROR("Failed to scan directory '%s'", cfg->dir);
		return -1;
	}

	LOG_DEBUG("nftw walk completed on '%s'", cfg->dir);
	return 0;
}
