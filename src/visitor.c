#define _XOPEN_SOURCE 700

#include "visitor.h"

#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

long G_found_count   = 0;
long G_deleted_count = 0;
long G_error_count   = 0;

static bool path_matches_glob(const char *path)
{
	if (strcmp(G_Arguments.glob, "**/*") == 0) return true;
	return fnmatch(G_Arguments.glob, path, 0) == 0;
}

static void delete_broken_link(const char *fpath)
{
	if (G_Arguments.delete_cmd) {
		char cmd[PATH_MAX + 256];
		int  n = snprintf(cmd, sizeof(cmd), "%s %s", G_Arguments.delete_cmd, fpath);

		if (n < 0 || (size_t)n >= sizeof(cmd)) {
			LOG_ERROR("Path too long to build delete command for '%s'", fpath);
			G_error_count++;
			return;
		}

		if (system(cmd) != 0) {
			LOG_ERROR("delete-cmd '%s' failed for '%s'", G_Arguments.delete_cmd, fpath);
			G_error_count++;
			return;
		}

		LOG_INFO("Deleted (via '%s'): %s", G_Arguments.delete_cmd, fpath);
		G_deleted_count++;
	} else {
		if (unlink(fpath) != 0) {
			LOG_PERROR("Failed to delete '%s'", fpath);
			G_error_count++;
			return;
		}

		LOG_INFO("Deleted: %s", fpath);
		G_deleted_count++;
	}
}

int visit(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	(void)sb;

	if (G_Arguments.max_depth >= 0 && ftwbuf->level > G_Arguments.max_depth)
		return 0;

	if (typeflag != FTW_SL) return 0;

	struct stat target;
	if (stat(fpath, &target) == 0) return 0;

	if (errno != ENOENT && errno != ENOTDIR && errno != ELOOP) {
		LOG_WARN("Could not verify target of '%s': %s", fpath, strerror(errno));
		return 0;
	}

	if (!path_matches_glob(fpath)) return 0;

	char target_buf[PATH_MAX];
	ssize_t len = readlink(fpath, target_buf, sizeof(target_buf) - 1);
	if (len >= 0) target_buf[len] = '\0';
	else          strcpy(target_buf, "?");

	G_found_count++;

	if (G_Arguments.print) {
		if (G_Arguments.null) {
			fputs(fpath, stdout);
			fputc('\0', stdout);
		} else {
			puts(fpath);
		}
		return 0;
	}

	if (G_Arguments.dry_run) {
		LOG_INFO("[dry-run] %s -> %s", fpath, target_buf);
	} else if (G_Arguments.delete) {
		LOG_DEBUG("Found broken symlink: %s -> %s", fpath, target_buf);
		delete_broken_link(fpath);
	} else {
		LOG_INFO("%s -> %s", fpath, target_buf);
	}

	return 0;
}
