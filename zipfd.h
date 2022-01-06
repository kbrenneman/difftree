#ifndef ZIPFD_H
#define ZIPFD_H

/**
 * Functions to deal with multiple zip_t structs for the same file.
 *
 * We need to use multiple zip_t structs because they're not thread-safe, and
 * we read zip entries on a worker thread.
 *
 * This uses a zip_t source that uses pread(2) to read data, which allows
 * multiple zip_t structs to share the same file descriptor.
 *
 * The DtZipFile struct then caches multiple zip_t struct, so that they can be
 * reused.
 */

#include <zip.h>

#include "ref-count-struct.h"

#ifdef __cplusplus
{
#endif

typedef struct DtZipFileRec DtZipFile;

UTIL_DECLARE_BOXED_REFCOUNT_FUNCS(DtZipFile, dt_zip_file);

// TODO: Use a GError to hand back error conditions?

/**
 * Creates a new DtZipFile object.
 *
 * \param fd The file descriptor for the zip file. This will be closed when the
 *      DtZipFile is destroyed.
 * \param cache_size The maximum number of zip_t objects to cache.
 */
DtZipFile *dt_zip_file_new(int fd, size_t cache_size);

/**
 * Sets the maximum number of cached zip_t objects.
 */
void dt_zip_file_set_cache_size(DtZipFile *self, size_t size);

/**
 * Returns the maximum number of cached zip_t objects.
 */
size_t dt_zip_file_get_cache_size(DtZipFile *self);

/**
 * Returns a zip_t object. This will return a cached zip_t if one is available.
 * Otherwise, it will return a new one.
 *
 * When the caller is finished with the zip_t, then it must either be closed
 * with zip_discard(), or released with dt_zip_file_return_zipfile.
 */
zip_t *dt_zip_file_get_zipfile(DtZipFile *self, zip_error_t *error);

/**
 * Returns a zip_t object to the cache.
 */
void dt_zip_file_return_zipfile(DtZipFile *self, zip_t *zf);

#ifdef __cplusplus
}
#endif

#endif // ZIPFD_H
