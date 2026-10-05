/*
 * os_backend.h - the filesystem concerns that cannot be portable. A platform
 * that routes them (Android = AAssetManager) provides real implementations,
 * everyone else gets the no-op defaults in os.c.
 */
#ifndef OS_BACKEND_H
#define OS_BACKEND_H

#include "platform.h"

/* Shipped asset as a read-only stream, or NULL when not routed or missing. */
IoStream *os_backend_asset_open(const char *path);

/* 1 exists, 0 missing, -1 not routed (os.c then checks the filesystem). */
int os_backend_asset_exists(const char *path);

/* Writable per-app directory, or NULL when not provided (os.c uses dir_app). */
const char *os_backend_data_dir(void);

#endif /* OS_BACKEND_H */
