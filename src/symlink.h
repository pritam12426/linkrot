#ifndef _SYMLINK_H_
#define _SYMLINK_H_

#include <stdbool.h>
#include <stddef.h>

bool symlink_matches_glob(const char *path, const char *pattern);
bool symlink_is_broken(const char *path, char *target_buf, size_t target_len);
int  symlink_delete(const char *path, const char *delete_cmd);

#endif  // _SYMLINK_H_
