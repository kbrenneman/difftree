#include "zipfd.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>

#include <zip.h>

/**
 * Shared data between DtZipFile and DtZipFileSourceData objects.
 *
 * This is separate from DtZipFile so that we don't have a circular reference
 * between the DtZipFile and the zip_t objects in the cache.
 */
typedef struct
{
    UtilRefCountedBase refcount;

    int fd;
    off_t file_size;
} DtZipFileShared;

struct DtZipFileRec
{
    UtilRefCountedBase refcount;

    GMutex mutex;
    DtZipFileShared *shared;

    GSList *zip_cache;
    size_t zip_cache_max;
    size_t zip_cache_size;
};

typedef struct
{
    DtZipFileShared *shared;

    zip_error_t error;
    off_t current_offset;
} DtZipFileSourceData;

static void dt_zip_file_free(DtZipFile *self);
static void dt_zip_file_shared_free(DtZipFileShared *self);
static zip_int64_t zipfd_source_callback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd);

UTIL_DEFINE_BOXED_REFCOUNT_TYPE(DtZipFile, dt_zip_file, dt_zip_file_free);
UTIL_DEFINE_BOXED_REFCOUNT_TYPE(DtZipFileShared, dt_zip_file_shared, dt_zip_file_shared_free);

static DtZipFileShared *dt_zip_file_shared_new(int fd)
{
    DtZipFileShared *self;
    struct stat st;

    if (fstat(fd, &st) != 0)
    {
        g_critical("fstat failed: %s\n", strerror(errno));
        return NULL;
    }

    self = g_malloc(sizeof(DtZipFileShared));
    util_ref_counted_struct_init(&self->refcount);
    self->fd = fd;
    self->file_size = st.st_size;

    return self;
}

static void dt_zip_file_shared_free(DtZipFileShared *self)
{
    if (self != NULL)
    {
        if (self->fd >= 0)
        {
            close(self->fd);
        }
        g_free(self);
    }
}

DtZipFile *dt_zip_file_new(int fd, size_t cache_size)
{
    DtZipFileShared *shared;
    DtZipFile *self;

    shared = dt_zip_file_shared_new(fd);
    if (shared == NULL)
    {
        return NULL;
    }

    self = g_malloc0(sizeof(DtZipFile));
    g_mutex_init(&self->mutex);
    util_ref_counted_struct_init(&self->refcount);
    self->shared = shared;
    self->zip_cache_max = cache_size;
    return self;
}

void dt_zip_file_set_cache_size(DtZipFile *self, size_t size)
{
    g_mutex_lock(&self->mutex);

    self->zip_cache_max = size;

    // If the max size is lower, then discard any excess entries.
    while (self->zip_cache_size > self->zip_cache_max)
    {
        // TODO: Move the nodes to a local list, and then free them after unlocking the mutex.
        GSList *node = self->zip_cache;
        zip_t *zip = node->data;
        self->zip_cache = g_slist_remove_link(self->zip_cache, node);
        g_slist_free_1(node);

        zip_discard(zip);
    }
    g_mutex_unlock(&self->mutex);
}

size_t dt_zip_file_get_cache_size(DtZipFile *self)
{
    return self->zip_cache_max;
}

static void dt_zip_file_free(DtZipFile *self)
{
    if (self != NULL)
    {
        while (self->zip_cache != NULL)
        {
            GSList *node = self->zip_cache;
            zip_t *zip = node->data;
            self->zip_cache = g_slist_remove_link(self->zip_cache, node);
            g_slist_free_1(node);

            zip_discard(zip);
        }

        dt_zip_file_shared_unref(self->shared);
        g_mutex_clear(&self->mutex);
        g_free(self);
    }
}

static void zipfd_source_cleanup(DtZipFileSourceData *zsd)
{
    if (zsd != NULL)
    {
        dt_zip_file_shared_unref(zsd->shared);
        zip_error_fini(&zsd->error);
        g_free(zsd);
    }
}

static zip_t *dt_zip_file_create_zipfile(DtZipFile *self, zip_error_t *error)
{
    DtZipFileSourceData *zsd;
    zip_source_t *source = NULL;
    zip_t *arch = NULL;

    zsd = g_malloc0(sizeof(DtZipFileSourceData));
    zsd->shared = dt_zip_file_shared_ref(self->shared);
    zip_error_init(&zsd->error);

    source = zip_source_function_create(zipfd_source_callback, zsd, error);
    if (source == NULL)
    {
        zipfd_source_cleanup(zsd);
        return NULL;
    }

    arch = zip_open_from_source(source, ZIP_RDONLY, error);
    if (arch == NULL)
    {
        // TODO: Will this free the DtZipFileSourceData struct?
        zip_source_close(source);
        return NULL;
    }

    return arch;
}

zip_t *dt_zip_file_get_zipfile(DtZipFile *self, zip_error_t *error)
{
    zip_t *zip = NULL;

    g_mutex_lock(&self->mutex);
    if (self->zip_cache_size > 0)
    {
        GSList *node = self->zip_cache;
        zip = node->data;

        g_assert(node != NULL);
        self->zip_cache = g_slist_remove_link(self->zip_cache, node);
        g_slist_free_1(node);
        self->zip_cache_size--;

        g_debug("Reusing zip_t\n");
    }
    g_mutex_unlock(&self->mutex);

    if (zip == NULL)
    {
        // We didn't have a cached zip_t, so open a new one.
        g_debug("Creating new zip_t\n");
        zip = dt_zip_file_create_zipfile(self, error);
    }
    return zip;
}

void dt_zip_file_return_zipfile(DtZipFile *self, zip_t *zip)
{
    if (zip != NULL)
    {
        if (self != NULL)
        {
            g_mutex_lock(&self->mutex);
            if (self->zip_cache_size < self->zip_cache_max)
            {
                self->zip_cache = g_slist_prepend(self->zip_cache, zip);
                self->zip_cache_size++;
                zip = NULL;
            }
            g_mutex_unlock(&self->mutex);
        }

        if (zip != NULL)
        {
            g_debug("Discarding zip_t\n");
            zip_discard(zip);
        }
    }
}

static zip_int64_t zipfd_source_callback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd)
{
    DtZipFileSourceData *zsd = userdata;

    if (cmd == ZIP_SOURCE_SUPPORTS)
    {
        zip_int64_t ret = zip_source_make_command_bitmap(
                // Base functions for a readable source
                //ZIP_SOURCE_ACCEPT_EMPTY,
                ZIP_SOURCE_OPEN,
                ZIP_SOURCE_READ,
                ZIP_SOURCE_CLOSE,
                ZIP_SOURCE_STAT,
                ZIP_SOURCE_ERROR,
                // Additional functions for a seekable read source
                ZIP_SOURCE_SEEK,
                ZIP_SOURCE_TELL,
                ZIP_SOURCE_SUPPORTS,

                // Other things that we support
                ZIP_SOURCE_FREE,
                -1);
        return ret;
    }
    else if (cmd == ZIP_SOURCE_OPEN)
    {
        // Prepare for reading. We already opened the file, so nothing else to do here.
        return 0;
    }
    else if (cmd == ZIP_SOURCE_CLOSE)
    {
        // Reading is done. I guess this is where it might close the file?
        return 0;
    }
    else if (cmd == ZIP_SOURCE_FREE)
    {
        // Clean up all resources, including userdata. The callback will not be called again after this.
        zipfd_source_cleanup(zsd);
        return 0;
    }
    else if (cmd == ZIP_SOURCE_READ)
    {
        ssize_t n = pread(zsd->shared->fd, data, len, zsd->current_offset);
        if (n < 0)
        {
            zip_error_set(&zsd->error, ZIP_ER_READ, errno);
            return -1;
        }
        return n;
    }
    else if (cmd == ZIP_SOURCE_STAT)
    {
        // Here, I can fill in a zip_stat_t struct with various details about the file.
        zip_stat_t *st = data;
        assert(len >= sizeof(zip_stat_t));
        zip_stat_init(st);

        st->size = zsd->shared->file_size;
        st->valid |= ZIP_STAT_SIZE;
        return sizeof(struct zip_stat);
    }
    else if (cmd == ZIP_SOURCE_ERROR)
    {
        return zip_error_to_data(&zsd->error, data, len);
    }
    else if (cmd == ZIP_SOURCE_SEEK)
    {
        zip_int64_t newoffset = zip_source_seek_compute_offset(zsd->current_offset, zsd->shared->file_size, data, len, &zsd->error);
        if (newoffset < 0)
        {
            g_critical("zip_source_seek_compute_offset failed?\n");
            return -1;
        }
        zsd->current_offset = newoffset;
        return 0;
    }
    else if (cmd == ZIP_SOURCE_TELL)
    {
        return zsd->current_offset;
    }
    else
    {
        zip_error_set(&zsd->error, ZIP_ER_OPNOTSUPP, 0);
        return -1;
    }
}
