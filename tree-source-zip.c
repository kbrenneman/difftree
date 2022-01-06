#include "tree-source-zip.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <gio/gio.h>

#include <zip.h>

#include "zipfd.h"
#include "zip-input-stream.h"

#define ATTRIB_FILE_ARCHIVE_INDEX "dt::zipfile:archive_index"
#define ATTRIB_FILE_ARCHIVE_PATH "dt::zipfile:archive_path"

struct _DtTreeSourceZip
{
    DtTreeSourceBase parent_instance;

    char **prefix;
    gint prefix_len;

    DtZipFile *zipsource;
};

static void dt_tree_source_zip_interface_init(DtTreeSourceInterface *iface);
static void dt_tree_source_zip_dispose(GObject *gobj);
static void dt_tree_source_zip_finalize(GObject *gobj);

static void dt_tree_source_zip_open_file_async(DtTreeSource *self, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata);
static GInputStream *dt_tree_source_zip_open_file_finish(DtTreeSource *self, GAsyncResult *res, GError **error);
static GInputStream *dt_tree_source_zip_open_file(DtTreeSource *self, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error);

G_DEFINE_TYPE_WITH_CODE(DtTreeSourceZip, dt_tree_source_zip, DT_TYPE_TREE_SOURCE_BASE,
        G_IMPLEMENT_INTERFACE(DT_TYPE_TREE_SOURCE, dt_tree_source_zip_interface_init));

static void dt_tree_source_zip_interface_init(DtTreeSourceInterface *iface)
{
    iface->open_file = dt_tree_source_zip_open_file;
    iface->open_file_async = dt_tree_source_zip_open_file_async;
    iface->open_file_finish = dt_tree_source_zip_open_file_finish;
}

static void dt_tree_source_zip_class_init(DtTreeSourceZipClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = dt_tree_source_zip_dispose;
    object_class->finalize = dt_tree_source_zip_finalize;
}

static gint remove_empty_strings(char **strings)
{
    gint src;
    gint dst = 0;
    for (src = 0; strings[src] != NULL; src++)
    {
        if (strings[src][0] != '\x00')
        {
            strings[dst] = strings[src];
            dst++;
        }
        else
        {
            g_free(strings[src]);
        }
    }
    strings[dst] = NULL;
    return dst;
}

static DtTreeSourceNode *add_member(DtTreeSourceZip *self, const char *path, GFileInfo *info)
{
    char **pathElems = g_strsplit(path, "/", 0);
    gint pathLen = remove_empty_strings(pathElems);
    DtTreeSourceNode *parent, *node;
    gint i;

    if (pathLen <= self->prefix_len)
    {
        g_strfreev(pathElems);
        return NULL;
    }

    // Skip over the prefix elements
    for (i=0; i<self->prefix_len; i++)
    {
        if (strcmp(pathElems[i], self->prefix[i]) != 0)
        {
            g_strfreev(pathElems);
            return NULL;
        }
    }

    // Add any missing parent nodes
    parent = dt_tree_source_get_root(DT_TREE_SOURCE(self));
    for (i=self->prefix_len; i<pathLen - 1; i++)
    {
        DtTreeSourceNode *nextParent = dt_tree_source_get_child_by_name(DT_TREE_SOURCE(self), parent, pathElems[i]);
        if (nextParent == NULL)
        {
            GFileInfo *parentInfo = g_file_info_new();
            g_file_info_set_name(parentInfo, pathElems[i]);
            g_file_info_set_display_name(parentInfo, pathElems[i]);
            g_file_info_set_file_type(parentInfo, G_FILE_TYPE_DIRECTORY);

            dt_tree_source_base_add_children(DT_TREE_SOURCE_BASE(self), parent,
                    1, &parentInfo, &nextParent);
            g_object_unref(parentInfo);
        }
        else
        {
            GFileInfo *parentInfo = dt_tree_source_get_file_info(DT_TREE_SOURCE(self), nextParent);
            if (g_file_info_get_file_type(parentInfo) != G_FILE_TYPE_DIRECTORY)
            {
                g_warning("Zip file contains children under non-directory for %s\n", path);
                g_strfreev(pathElems);
                return NULL;
            }
        }
        parent = nextParent;
    }

    g_file_info_set_name(info, pathElems[pathLen - 1]);
    g_file_info_set_display_name(info, pathElems[pathLen - 1]);

    // Add or replace the GFileInfo entry for this member.
    node = dt_tree_source_get_child_by_name(DT_TREE_SOURCE(self), parent, pathElems[pathLen - 1]);
    if (node == NULL)
    {
        dt_tree_source_base_add_children(DT_TREE_SOURCE_BASE(self), parent,
                1, &info, &node);
    }
    else
    {
        // There's already a row for this file. This could happen if we added a
        // file before its parent directory, or if the zip file contains
        // duplicate paths.
        GFileInfo *prevInfo = dt_tree_source_get_file_info(DT_TREE_SOURCE(self), node);
        if (g_file_info_get_file_type(prevInfo) != g_file_info_get_file_type(info))
        {
            g_warning("Zip file contains mismatched file type for %s\n", path);
            g_strfreev(pathElems);
            return NULL;
        }
        dt_tree_source_base_set_file_info(DT_TREE_SOURCE_BASE(self), node, info);
    }
    g_strfreev(pathElems);
    return node;
}

static gboolean scan_zip_file(DtTreeSourceZip *self, zip_t *zipfile)
{
    zip_int64_t i, num;
    gboolean foundAnyMatch = FALSE;

    num = zip_get_num_entries(zipfile, 0);
    for (i=0; i<num; i++)
    {
        zip_stat_t zst = {};

        // This will try to convert the filename to UTF-8, unless I give it the flag ZIP_FL_ENC_RAW.
        if (zip_stat_index(zipfile, i, 0, &zst) == 0)
        {
            GFileInfo *info = NULL;

            if (!(zst.valid & ZIP_STAT_NAME))
            {
                g_warning("Can't get name for ZIP entry %lld\n", (long long) i);
                continue;
            }

            info = g_file_info_new();
            g_file_info_set_attribute_int64(info, ATTRIB_FILE_ARCHIVE_INDEX, i);
            g_file_info_set_attribute_string(info, ATTRIB_FILE_ARCHIVE_PATH, zst.name);

            // Directories in a zip file are distinguished by a trailing '/'
            // character in the filename.
            if (g_str_has_suffix(zst.name, "/"))
            {
                g_file_info_set_file_type(info, G_FILE_TYPE_DIRECTORY);
            }
            else
            {
                if (!(zst.valid & ZIP_STAT_SIZE))
                {
                    g_warning("Can't get size for ZIP entry \"%s\"\n", zst.name);
                    g_object_unref(info);
                    continue;
                }

                g_file_info_set_file_type(info, G_FILE_TYPE_REGULAR);
                g_file_info_set_size(info, zst.size);
                if (zst.valid & ZIP_STAT_CRC)
                {
                    g_file_info_set_attribute_uint32(info, DT_FILE_ATTRIBUTE_CRC, zst.crc);
                }
            }
            if (zst.valid & ZIP_STAT_MTIME)
            {
                GTimeVal tv;
                tv.tv_sec = zst.mtime;
                tv.tv_usec = 0;
                g_file_info_set_modification_time(info, &tv);
            }

            add_member(self, zst.name, info);
            foundAnyMatch = TRUE;
            g_object_unref(info);
        }
    }

    return foundAnyMatch;
}

static void set_error_from_zip(GError **error, zip_error_t *ze)
{
    GIOErrorEnum gio_code = G_IO_ERROR_FAILED;
    if (zip_error_system_type(ze) == ZIP_ET_SYS)
    {
        gio_code = g_io_error_from_errno(zip_error_code_system(ze));
    }
    // Note: I could define a domain for the libzip error codes

    g_set_error(error, G_IO_ERROR, gio_code,
            "libzip error: %d %s", zip_error_code_zip(ze), zip_error_strerror(ze));
}

DtTreeSourceZip *dt_tree_source_zip_new(DtZipFile *zipsource, const char *subdir, GError **error)
{
    DtTreeSourceZip *self;
    zip_t *zipfile;
    zip_error_t ze;

    zip_error_init(&ze);
    zipfile = dt_zip_file_get_zipfile(zipsource, &ze);
    if (zipfile == NULL)
    {
        set_error_from_zip(error, &ze);
        zip_error_fini(&ze);
        return NULL;
    }
    zip_error_fini(&ze);

    self = g_object_new(DT_TYPE_TREE_SOURCE_ZIP, NULL);
    self->zipsource = dt_zip_file_ref(zipsource);
    if (subdir != NULL)
    {
        self->prefix = g_strsplit(subdir, "/", 0);
        self->prefix_len = remove_empty_strings(self->prefix);
    }
    else
    {
        self->prefix = g_malloc(sizeof(char *));
        self->prefix[0] = NULL;
        self->prefix_len = 0;
    }

    if (!scan_zip_file(self, zipfile))
    {
        // Note: This is before setting the should_close flag, so that we don't
        // close the file descriptor if there's an error.
        g_object_unref(self);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "No matching path inside zip file.\n");
        self = NULL;
    }
    dt_zip_file_return_zipfile(zipsource, zipfile);
    return self;
}

DtTreeSourceZip *dt_tree_source_zip_new_for_path(const char *path, const char *subdir, GError **error)
{
    int fd;
    DtZipFile *zipsource;
    DtTreeSourceZip *self;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        int err = errno;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err),
                "Can't open %s: %s", path, strerror(err));
        return NULL;
    }

    zipsource = dt_zip_file_new(fd, 2);
    if (zipsource == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Can't open zip file for %s\n", path);
        close(fd);
        return NULL;
    }

    self = dt_tree_source_zip_new(zipsource, subdir, error);
    dt_zip_file_unref(zipsource);
    return self;
}

static void dt_tree_source_zip_init(DtTreeSourceZip *self)
{
    self->prefix = NULL;
    self->prefix_len = 0;
    self->zipsource = NULL;
}
static void dt_tree_source_zip_dispose(GObject *gobj)
{
    G_OBJECT_CLASS(dt_tree_source_zip_parent_class)->dispose(gobj);
}
static void dt_tree_source_zip_finalize(GObject *gobj)
{
    DtTreeSourceZip *self = DT_TREE_SOURCE_ZIP(gobj);

    if (self->zipsource != NULL)
    {
        dt_zip_file_unref(self->zipsource);
        self->zipsource = NULL;
    }

    if (self->prefix != NULL)
    {
        g_strfreev(self->prefix);
        self->prefix = NULL;
    }

    G_OBJECT_CLASS(dt_tree_source_zip_parent_class)->finalize(gobj);
}

static void zip_stream_close_callback(DtZipInputStream *stream, zip_t *zip, gpointer data)
{
    g_debug("Closing/returning zip file\n");
    DtZipFile *zipsource = data;
    dt_zip_file_return_zipfile(zipsource, zip);
    dt_zip_file_unref(zipsource);
}

static GInputStream *open_file_common(DtTreeSourceZip *self, GFileInfo *info, GError **error)
{
    DtZipInputStream *stream;
    zip_t *zipfile;
    zip_file_t *member;
    zip_int64_t index;
    zip_error_t ze;

    if (!g_file_info_has_attribute(info, ATTRIB_FILE_ARCHIVE_INDEX))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "File has no corresponding archive member\n");
        return NULL;
    }
    index = g_file_info_get_attribute_int64(info, ATTRIB_FILE_ARCHIVE_INDEX);

    zip_error_init(&ze);
    zipfile = dt_zip_file_get_zipfile(self->zipsource, &ze);
    if (zipfile == NULL)
    {
        set_error_from_zip(error, &ze);
        zip_error_fini(&ze);
        return NULL;
    }
    zip_error_fini(&ze);

    member = zip_fopen_index(zipfile, index, 0);
    if (member == NULL)
    {
        set_error_from_zip(error, zip_get_error(zipfile));
        dt_zip_file_return_zipfile(self->zipsource, zipfile);
        return NULL;
    }

    stream = dt_zip_input_stream_new(zipfile, member);
    dt_zip_input_stream_set_close_callback(stream, zip_stream_close_callback, dt_zip_file_ref(self->zipsource));
    return G_INPUT_STREAM(stream);
}

static void open_file_thread_proc(GTask *task, gpointer source_object,
        gpointer task_data, GCancellable *cancellable)
{
    GFileInfo *info = G_FILE_INFO(task_data);
    DtTreeSourceZip *self = DT_TREE_SOURCE_ZIP(source_object);
    GError *error = NULL;
    GInputStream *stream = open_file_common(self, info, &error);
    if (stream != NULL)
    {
        g_task_return_pointer(task, stream, g_object_unref);
    }
    else
    {
        g_task_return_error(task, error);
    }
}

static void dt_tree_source_zip_open_file_async(DtTreeSource *source, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata)
{
    GTask *task = NULL;
    GFileInfo *info = NULL;

    info = dt_tree_source_get_file_info(source, node);
    if (info == NULL || g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
    {
        g_task_report_new_error(source, callback, userdata, NULL,
                G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "File has no corresponding archive member\n");
        return;
    }

    task = g_task_new(source, cancellable, callback, userdata);
    g_task_set_priority(task, io_priority);

    // I'm not sure if GFileInfo is thread-safe, so just copy it instead
    // TODO: I think all I actually need is the member index, so I could just
    // pass that through.
    g_task_set_task_data(task, g_file_info_dup(info), g_object_unref);
    g_task_run_in_thread(task, open_file_thread_proc);

    // Unreference the GTask. I think it'll keep a reference while the worker
    // thread is running, until the main thread deals with the callback, and so
    // unreferencing it now ensures that it gets cleaned up if the caller
    // doesn't provide a callback function.
    g_object_unref(task);
}

static GInputStream *dt_tree_source_zip_open_file_finish(DtTreeSource *source, GAsyncResult *res, GError **error)
{
    GTask *task = G_TASK(res);
    gpointer ret = g_task_propagate_pointer(task, error);
    if (ret != NULL)
    {
        return G_INPUT_STREAM(ret);
    }
    else
    {
        return NULL;
    }
}

static GInputStream *dt_tree_source_zip_open_file(DtTreeSource *source, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error)
{
    GFileInfo *info = dt_tree_source_get_file_info(source, node);

    if (info == NULL || g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "File has no corresponding archive member\n");
        return NULL;
    }

    return open_file_common(DT_TREE_SOURCE_ZIP(source), info, error);
}

