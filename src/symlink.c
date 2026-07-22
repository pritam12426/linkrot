#define _XOPEN_SOURCE 700

#include "symlink.h"

#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

bool symlink_matches_glob(const char *path, const char *pattern)
{
	if (strcmp(pattern, "**/*") == 0) {
		LOG_TRACE("glob: pattern='**/*', auto-match '%s'", path);
		return true;
	}

	int ret = fnmatch(pattern, path, 0);
	LOG_TRACE("glob: pattern='%s' path='%s' result=%d (%s)",
	          pattern, path, ret, ret == 0 ? "match" : "no match");
	return ret == 0;
}

bool symlink_is_broken(const char *path, char *target_buf, size_t target_len)
{
	struct stat st;
	LOG_TRACE("stat: checking '%s'", path);

	if (stat(path, &st) == 0) {
		LOG_TRACE("stat: '%s' target exists (not broken)", path);
		return false;
	}

	LOG_TRACE("stat: '%s' failed (errno=%d: %s)", path, errno, strerror(errno));

	if (errno != ENOENT && errno != ENOTDIR && errno != ELOOP) {
		LOG_WARN("Could not verify target of '%s': %s", path, strerror(errno));
		return false;
	}

	LOG_TRACE("readlink: reading target of '%s'", path);
	ssize_t len = readlink(path, target_buf, target_len - 1);
	if (len >= 0) {
		target_buf[len] = '\0';
		LOG_TRACE("readlink: '%s' -> '%s'", path, target_buf);
	} else {
		LOG_PERROR("readlink failed for '%s'", path);
		snprintf(target_buf, target_len, "?");
	}

	return true;
}

int symlink_delete(const char *path, const char *delete_cmd)
{
	if (delete_cmd) {
		LOG_TRACE("fork: launching delete-cmd '%s' for '%s'", delete_cmd, path);

		pid_t pid = fork();
		if (pid == 0) {
			execlp(delete_cmd, delete_cmd, path, (char *)NULL);
			_exit(127);
		} else if (pid < 0) {
			LOG_PERROR("fork failed for delete-cmd '%s'", delete_cmd);
			return -1;
		}

		LOG_TRACE("waitpid: waiting for child pid=%d", pid);

		int status;
		if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			LOG_ERROR("delete-cmd '%s' failed for '%s'", delete_cmd, path);
			return -1;
		}

		LOG_DEBUG("delete-cmd '%s' succeeded for '%s' (exit=%d)", delete_cmd, path, WEXITSTATUS(status));
		LOG_INFO("Deleted (via '%s'): %s", delete_cmd, path);
		return 0;
	}

	LOG_TRACE("unlink: deleting '%s'", path);

	if (unlink(path) != 0) {
		LOG_PERROR("Failed to delete '%s'", path);
		return -1;
	}

	LOG_DEBUG("unlink: '%s' deleted successfully", path);
	LOG_INFO("Deleted: %s", path);
	return 0;
}
