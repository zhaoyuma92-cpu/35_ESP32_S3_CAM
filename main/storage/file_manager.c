#include "file_manager.h"

#include <strings.h>
#include <string.h>

bool file_name_is_safe(const char *name)
{
    if (!name || name[0] == '\0') {
        return false;
    }
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
        return false;
    }
    if (strcasecmp(name, "System Volume Information") == 0) {
        return false;
    }
    return true;
}

bool file_name_is_downloadable(const char *name)
{
    if (!file_name_is_safe(name)) {
        return false;
    }
    const char *ext = strrchr(name, '.');
    if (!ext) {
        return false;
    }
    return strcasecmp(ext, ".csv") == 0 ||
           strcasecmp(ext, ".log") == 0 ||
           strcasecmp(ext, ".json") == 0 ||
           strcasecmp(ext, ".pgm") == 0;
}

const char *file_content_type(const char *name)
{
    const char *ext = name ? strrchr(name, '.') : NULL;
    if (ext && strcasecmp(ext, ".csv") == 0) {
        return "text/csv";
    }
    if (ext && strcasecmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (ext && strcasecmp(ext, ".pgm") == 0) {
        return "image/x-portable-graymap";
    }
    return "application/octet-stream";
}
