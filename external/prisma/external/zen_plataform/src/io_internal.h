#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "platform.h"

typedef struct
{
    int64_t (*size)(IoStream *s);
    int64_t (*seek)(IoStream *s, int64_t offset, IoWhence whence);
    size_t (*read)(IoStream *s, void *dst, size_t n);
    size_t (*write)(IoStream *s, const void *src, size_t n);
    bool (*flush)(IoStream *s);
    bool (*close)(IoStream *s);
} IoVTable;

struct IoStream
{
    const IoVTable *vt;
    bool eof;
    union
    {
        int fd;
        void *handle;
        struct
        {
            const uint8_t *base;
            size_t size;
            size_t pos;
        } mem;
    } u;
};

IoStream *io_stream_alloc(const IoVTable *vt);

typedef enum
{
    IO_MODE_READ = 1,
    IO_MODE_WRITE = 2,
    IO_MODE_APPEND = 4,
    IO_MODE_TRUNCATE = 8,
    IO_MODE_CREATE = 16
} IoModeFlags;

int io_parse_mode(const char *mode);

IoStream *io_platform_open_file(const char *path, int flags);
bool io_platform_replace_file(const char *from, const char *to);
bool io_platform_remove_file(const char *path);

#endif /* IO_INTERNAL_H */
