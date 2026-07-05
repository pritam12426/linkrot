#ifndef _VISITOR_H_
#define _VISITOR_H_

#include <stdbool.h>
#include <sys/stat.h>
#include <ftw.h>

#include "log.h"

typedef struct {
	bool        dry_run;
	bool        delete;
	bool        print;
	bool        null;
	int         max_depth;
	const char *dir;
	const char *glob;
	const char *delete_cmd;
	Log_level_t log_level;
} Arguments;

extern Arguments G_Arguments;

extern long G_found_count;
extern long G_deleted_count;
extern long G_error_count;

int visit(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

#endif  // _VISITOR_H_
