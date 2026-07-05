#define _XOPEN_SOURCE 700

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>

#include "log.h"
#include "project_config.h"
#include "visitor.h"

const char *argp_program_version     = MAIN_BINARY " " PROJECT_VERSION;
const char *argp_program_bug_address = PROJECT_HOMEPAGE_URL "/issues" "\n" AUTH_MESSAGE;
static char doc[]                    = MAIN_BINARY " - " PROJECT_SHORT_DESC;

static struct argp_option options[] = {
	{ "depth",     'L', "DEPTH", 0, "Maximum traversal depth (0 = current dir only)"      },
	{ "log-level", 'l', "LEVEL", 0, "Log level: error, warn, info, debug (default: info)" },
	{ "glob",      'G', "GLOB",  0, "Glob pattern to match paths (default: **/*)"         },
	{ "dry-run",   'n', 0,       0, "Preview deletions without executing them"            },
	{ "delete-cmd",'C', "CMD",   0, "Custom delete command (e.g., trash)"                 },
	{ "delete",    'D', 0,       0, "Permanently delete broken symlinks"                  },
	{ "print",     'P', 0,       0, "Print broken symlink paths only"                     },
	{ "null",      '0', 0,       0, "Null-separate output (for xargs -0)"                 },

	{ 0 }
};

Arguments G_Arguments = {
	.dry_run       = false,
	.delete        = false,
	.print         = false,
	.null          = false,
	.max_depth     = -1,
	.dir           = ".",
	.glob          = "**/*",
	.delete_cmd    = NULL,

	.log_level     = LOG_LEVEL_INFO
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	switch (key) {
		case 'G': G_Arguments.glob       = arg;  break;
		case 'n': G_Arguments.dry_run    = true; break;
		case 'D': G_Arguments.delete     = true; break;
		case 'C': G_Arguments.delete_cmd = arg;  break;
		case 'L':
		{
			char *end;
			long val = strtol(arg, &end, 10);
			if (*end != '\0' || val < 0)
				argp_error(state, "Invalid depth: '%s'. Must be a non-negative integer.", arg);
			G_Arguments.max_depth = (int)val;
			break;
		}
		case 'l':
			if      (strcmp(arg, "error") == 0) log_set_level(LOG_LEVEL_ERROR);
			else if (strcmp(arg, "warn")  == 0) log_set_level(LOG_LEVEL_WARN);
			else if (strcmp(arg, "info")  == 0) log_set_level(LOG_LEVEL_INFO);
			else if (strcmp(arg, "debug") == 0) log_set_level(LOG_LEVEL_DEBUG);
			else     argp_error(state, "Invalid log level: '%s'. Use: error, warn, info, debug.", arg);
			G_Arguments.log_level = log_get_level();
			break;
		case 'P': G_Arguments.print     = true; break;
		case '0': G_Arguments.null      = true; break;
		case ARGP_KEY_ARG:
			if (state->arg_num == 0)
				G_Arguments.dir = arg;
			else
				argp_error(state, "too many arguments: '%s'", arg);
			break;
		case ARGP_KEY_END: break;
		default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { .options = options, .parser = parse_opt, .args_doc = "[DIR]", .doc = doc };

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

	int nfds = 64;
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
