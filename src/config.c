#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "config.h"

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "project_config.h"

extern const char *argp_program_version;
extern const char *argp_program_bug_address;

static char doc[] = MAIN_BINARY " - " PROJECT_SHORT_DESC;

static struct argp_option options[] = {
	{ "depth",      'L', "DEPTH",  0, "Maximum traversal depth (0 = current dir only)"                         },
	{ "log-level",  'l', "LEVEL",  0, "Set log level: [off|fatal|error|warn|info|debug|trace] (default: info)" },
	{ "glob",       'G', "GLOB",   0, "Glob pattern to match paths (default: **/*)"                            },
	{ "dry-run",    'n', 0,        0, "Preview deletions without executing them"                               },
	{ "delete-cmd", 'C', "CMD",    0, "Custom delete command (e.g., trash)"                                    },
	{ "delete",     'D', 0,        0, "Permanently delete broken symlinks"                                     },
	{ "print",      'P', 0,        0, "Print broken symlink paths only"                                        },
	{ "null",       '0', 0,        0, "Null-separate output (for xargs -0)"                                    },
	{ "workers",    'j', "NUM",    0, "Number of worker threads (default: CPU count)"                          },
	{ 0 }
};

static Config cfg = {
	.dry_run       = false,
	.delete        = false,
	.print         = false,
	.null          = false,
	.max_depth     = -1,
	.num_workers   = 0,
	.dir           = ".",
	.glob          = "**/*",
	.delete_cmd    = NULL,

	.log_level     = LOG_LEVEL_INFO,
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	switch (key) {
		case 'G':
			LOG_TRACE("parsing -G/--glob: %s", arg);
			cfg.glob = arg;
			break;
		case 'n':
			LOG_TRACE("parsing -n/--dry-run");
			cfg.dry_run = true;
			break;
		case 'D':
			LOG_TRACE("parsing -D/--delete");
			cfg.delete = true;
			break;
		case 'C': {
			if (arg[0] == '\0')
				argp_error(state, "--delete-cmd cannot be empty");
			LOG_TRACE("parsing -C/--delete-cmd: %s", arg);
			cfg.delete_cmd = arg;
			break;
		}
		case 'L': {
			char *end;
			long val = strtol(arg, &end, 10);
			if (*end != '\0' || val < 0)
				argp_error(state, "Invalid depth: '%s'. Must be a non-negative integer.", arg);
			LOG_TRACE("parsing -L/--depth: %ld", val);
			cfg.max_depth = (int)val;
			break;
		}
		case 'l': {
			if      (strcmp(arg, "off")   == 0) cfg.log_level = LOG_LEVEL_OFF;
			else if (strcmp(arg, "fatal") == 0) cfg.log_level = LOG_LEVEL_FATAL;
			else if (strcmp(arg, "error") == 0) cfg.log_level = LOG_LEVEL_ERROR;
			else if (strcmp(arg, "warn")  == 0) cfg.log_level = LOG_LEVEL_WARN;
			else if (strcmp(arg, "info")  == 0) cfg.log_level = LOG_LEVEL_INFO;
			else if (strcmp(arg, "debug") == 0) cfg.log_level = LOG_LEVEL_DEBUG;
			else if (strcmp(arg, "trace") == 0) cfg.log_level = LOG_LEVEL_TRACE;
			else argp_error(state, "Invalid log level: '%s'. Use: off, fatal, error, warn, info, debug, trace.", arg);
			LOG_TRACE("parsing -l/--log-level: %s (enum=%d)", arg, cfg.log_level);
			break;
		}
		case 'j': {
			char *end;
			long val = strtol(arg, &end, 10);
			if (*end != '\0' || val < 1 || val > 128)
				argp_error(state, "Invalid worker count: '%s'. Must be 1-128.", arg);
			LOG_TRACE("parsing -j/--workers: %ld", val);
			cfg.num_workers = (int)val;
			break;
		}
		case 'P':
			LOG_TRACE("parsing -P/--print");
			cfg.print = true;
			break;
		case '0':
			LOG_TRACE("parsing -0/--null");
			cfg.null = true;
			break;
		case ARGP_KEY_ARG: {
			if (state->arg_num == 0) {
				LOG_TRACE("parsing positional arg [DIR]: %s", arg);
				cfg.dir = arg;
			} else {
				argp_error(state, "too many arguments: '%s'", arg);
			}
			break;
		}
		case ARGP_KEY_END: break;
		default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { .options = options, .parser = parse_opt, .args_doc = "[DIR]", .doc = doc };

Config *config_parse(int argc, char **argv)
{
	LOG_TRACE("invoking argp_parse");
	argp_parse(&argp, argc, argv, 0, 0, 0);

	if (cfg.num_workers == 0) {
		cfg.num_workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
		LOG_DEBUG("auto-detected %d CPU cores", cfg.num_workers);
	}
	if (cfg.num_workers < 1)
		cfg.num_workers = 1;

	int mode_count = (int)cfg.delete + (int)cfg.print + (cfg.delete_cmd != NULL);
	if (mode_count > 1) {
		fprintf(stderr, "linkrot: flags -D, -P, and -C are mutually exclusive\n");
		exit(1);
	}
	if (cfg.dry_run && !cfg.delete && !cfg.delete_cmd) {
		fprintf(stderr, "linkrot: -n (dry-run) requires -D or -C\n");
		exit(1);
	}

	LOG_DEBUG("config validated: dir=%s depth=%d workers=%d glob=%s",
	          cfg.dir, cfg.max_depth, cfg.num_workers, cfg.glob);

	return &cfg;
}
