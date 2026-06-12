#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

bool file_name_is_safe(const char *name);
bool file_name_is_downloadable(const char *name);
const char *file_content_type(const char *name);

#endif
