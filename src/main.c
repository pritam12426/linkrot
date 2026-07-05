/* Expose POSIX.1-2008 declarations (nftw, FTW_PHYS, PATH_MAX, readlink,
 * fileno, ...) even under a strict -std=c11 build. Must come before any
 * system header is included. */
#define _XOPEN_SOURCE 700

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ftw.h>
#include <fnmatch.h>

#include "log.h"
#include "project_config.h"

const char *argp_program_version     = MAIN_BINARY " " PROJECT_VERSION;
const char *argp_program_bug_address = PROJECT_HOMEPAGE_URL "/issues" "\n" AUTH_MESSAGE;
static char doc[]                    = MAIN_BINARY " - " PROJECT_SHORT_DESC;

static struct argp_option options[] = {
	{ "log-level",     'L', "LEVEL",   0, "Set log level: [error|warn|info|debug] (default: info)"  },
	{ "glob",          'G', "GLOB",    0, "Set custom glob string (default: \"**/*\", i.e. all files)" },
	{ "dry-run",       'n', 0,         0, "Show what would be deleted, without deleting anything"   },
	{ "delete-cmd",    'C', "CMD",     0, "Personalise a delete command (eg: trash)"                },
	{ "delete",        'D', 0,         0, "Permanently delete broken symlinks that are found"       },
	{ "dir",           'I', "DIR",     0, "Directory to scan (default: .)"                          },

	{ 0 }
};

typedef struct {
	bool        dry_run;
	bool        delete;
	const char *dir;
	const char *glob;
	const char *delete_cmd;

	Log_level_t log_level;
} Arguments;

static Arguments G_Arguments = {
	.dry_run       = false,
	.delete        = false,
	.dir           = ".",
	.glob          = "**/*",
	.delete_cmd    = NULL,

	.log_level     = LOG_LEVEL_INFO
};

/* Running totals, updated by the nftw() callback. */
static long G_found_count   = 0;
static long G_deleted_count = 0;
static long G_error_count   = 0;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	switch (key) {
		case 'I': G_Arguments.dir        = arg;  break;
		case 'G': G_Arguments.glob       = arg;  break;
		case 'n': G_Arguments.dry_run    = true; break;
		case 'D': G_Arguments.delete     = true; break;
		case 'C': G_Arguments.delete_cmd = arg;  break;
		case 'L':
			if      (strcmp(arg, "error") == 0) log_set_level(LOG_LEVEL_ERROR);
			else if (strcmp(arg, "warn")  == 0) log_set_level(LOG_LEVEL_WARN);
			else if (strcmp(arg, "info")  == 0) log_set_level(LOG_LEVEL_INFO);
			else if (strcmp(arg, "debug") == 0) log_set_level(LOG_LEVEL_DEBUG);
			else     argp_error(state, "Invalid log level: '%s'. Use: error, warn, info, debug.", arg);
			G_Arguments.log_level = log_get_level();
			break;
		case ARGP_KEY_END: break;
		default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { .options = options, .parser = parse_opt, .doc = doc };

/* --------------------------------------------------
 * Helpers
 * -------------------------------------------------- */

/* The default glob (match-everything sentinel, two stars then slash then
 * star) is treated specially; anything else is passed to fnmatch() against
 * the full (relative) path so patterns like "*.png" or "assets-dir*"
 * work without requiring FNM_PATHNAME semantics. */
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

/* nftw() callback: examine each filesystem entry under G_Arguments.dir.
 * FTW_PHYS means nftw() does NOT follow symlinks itself, so a symlink is
 * always reported as FTW_SL regardless of whether its target exists;
 * we resolve that ourselves with stat(), which is exactly the "test -e"
 * check we need. */
static int visit(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	(void)sb;
	(void)ftwbuf;

	if (typeflag != FTW_SL) return 0;

	struct stat target;
	if (stat(fpath, &target) == 0) return 0; /* target exists: not broken */

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

int main(int argc, char *argv[])
{
	argp_parse(&argp, argc, argv, 0, 0, 0);

	if (log_get_level() == LOG_LEVEL_DEBUG) {
		LOG_CUSTOM(LOG_LEVEL_DEBUG, false, "Command-line args: [");
		for (int i = 0; i < argc; i++) {
			fprintf(stderr, "\"%s\"", argv[i]);
			if (i != argc - 1) fputs(", ", stderr);
		}
		fputs("]\n", stderr);
	}

	/* FTW_PHYS: don't follow symlinks (needed so we can detect them as
	 *           symlinks rather than silently following into their targets)
	 * FTW_MOUNT is intentionally NOT set, so we do cross mount points;
	 * pass it too if that's ever undesirable. */
	int nfds = 64; /* max simultaneously open file descriptors used by nftw */
	if (nftw(G_Arguments.dir, visit, nfds, FTW_PHYS) == -1) {
		LOG_PERROR("Failed to scan directory '%s'", G_Arguments.dir);
		return 1;
	}

	if (G_Arguments.delete) {
		LOG_INFO("Found %ld broken symlink(s), deleted %ld, %ld error(s).",
		         G_found_count, G_deleted_count, G_error_count);
	} else {
		LOG_INFO("Found %ld broken symlink(s).", G_found_count);
	}

	return (G_error_count > 0) ? 1 : 0;
}
