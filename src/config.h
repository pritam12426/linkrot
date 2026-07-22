#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdbool.h>

#include "log.h"

typedef struct {
	bool        dry_run;
	bool        delete;
	bool        print;
	bool        null;
	int         max_depth;
	int         num_workers;
	const char *dir;
	const char *glob;
	const char *delete_cmd;
	Log_level_t log_level;
} Config;

Config *config_parse(int argc, char **argv);

#endif  // _CONFIG_H_
