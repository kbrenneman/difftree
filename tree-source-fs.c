#include "tree-source-fs.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <gio/gio.h>

struct _DtTreeSourceFS
{
    DtTreeSourceBase parent_instance;
    GFile *base;
    gboolean follow_symlinks;
};

typedef struct
{
    GQueue queue;
} DtTreeSourceFSScanState;

enum
{
    PROP_BASE = 1,
    PROP_FOLLOW_SYMLINKS,
    N_PROPERTIES
};
static GParamSpec *obj_properties[N_PROPERTIES] = {};

static const char *FILE_QUERY_ATTRIBS =
            G_FILE_ATTRIBUTE_STANDARD_TYPE
            "," G_FILE_ATTRIBUTE_STANDARD_NAME
            "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME
            "," G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET
            "," G_FILE_ATTRIBUTE_STANDARD_SIZE
            "," G_FILE_ATTRIBUTE_TIME_MODIFIED
            "," G_FILE_ATTRIBUTE_UNIX_MODE;

#define QUERY_BATCH_SIZE 1

static void dt_tree_source_fs_interface_init(DtTreeSourceInterface *iface);
static void dt_tree_source_fs_dispose(GObject *gobj);
static void dt_tree_source_fs_finalize(GObject *gobj);

static void dt_tree_source_fs_open_file_async(DtTreeSource *self, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata);
static GInputStream *dt_tree_source_fs_open_file_finish(DtTreeSource *self, GAsyncResult *res, GError **error);
static GInputStream *dt_tree_source_fs_open_file(DtTreeSource *self, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error);

static void dt_tree_source_fs_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata);
static gboolean dt_tree_source_fs_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error);

G_DEFINE_TYPE_WITH_CODE(DtTreeSourceFS, dt_tree_source_fs, DT_TYPE_TREE_SOURCE_BASE,
        G_IMPLEMENT_INTERFACE(DT_TYPE_TREE_SOURCE, dt_tree_source_fs_interface_init));

static void dt_tree_source_fs_interface_init(DtTreeSourceInterface *iface)
{
    iface->open_file = dt_tree_source_fs_open_file;
    iface->open_file_async = dt_tree_source_fs_open_file_async;
    iface->open_file_finish = dt_tree_source_fs_open_file_finish;
    iface->scan_async = dt_tree_source_fs_scan_async;
    iface->scan_finish = dt_tree_source_fs_scan_finish;
}

static void dt_tree_source_fs_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(object);
    switch (property_id)
    {
        case PROP_BASE:
            g_set_object(&self->base, G_FILE(g_value_get_object(value)));
            break;
        case PROP_FOLLOW_SYMLINKS:
            self->follow_symlinks = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void dt_tree_source_fs_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(object);
    switch (property_id)
    {
        case PROP_BASE:
            g_value_set_object(value, G_OBJECT(self->base));
            break;
        case PROP_FOLLOW_SYMLINKS:
            g_value_set_boolean(value, self->follow_symlinks);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void dt_tree_source_fs_class_init(DtTreeSourceFSClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = dt_tree_source_fs_set_property;
    object_class->get_property = dt_tree_source_fs_get_property;
    obj_properties[PROP_BASE] = g_param_spec_object(
            "base",
            "Base path",
            "The directory to list files from",
            G_TYPE_FILE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    obj_properties[PROP_FOLLOW_SYMLINKS] = g_param_spec_boolean(
            "follow-symlinks",
            "Follow symlinks flag",
            "True if we should follow symlinks, instead of showing the symlink itself",
            TRUE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);

    object_class->dispose = dt_tree_source_fs_dispose;
    object_class->finalize = dt_tree_source_fs_finalize;
}

DtTreeSourceFS *dt_tree_source_fs_new(GFile *base, gboolean follow_symlinks)
{
    return g_object_new(DT_TYPE_TREE_SOURCE_FS,
            "base", base,
            "follow-symlinks", follow_symlinks,
            NULL);
}

static void dt_tree_source_fs_init(DtTreeSourceFS *self)
{
    self->base = g_file_new_for_path("/");
}
static void dt_tree_source_fs_dispose(GObject *gobj)
{
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(gobj);
    g_clear_object(&self->base);
    G_OBJECT_CLASS(dt_tree_source_fs_parent_class)->dispose(gobj);
}
static void dt_tree_source_fs_finalize(GObject *gobj)
{
    G_OBJECT_CLASS(dt_tree_source_fs_parent_class)->finalize(gobj);
}

static void file_enum_close_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GError *error = NULL;

    // TODO: Should I wait for all of the close operations to finish before I
    // let the dt_tree_source_fs_scan_async operation finish?
    if (!g_file_enumerator_close_finish(G_FILE_ENUMERATOR(sourceobj), res, &error))
    {
        g_critical("Failed to close file enumerator: %s\n", error->message);
        g_clear_error(&error);
    }
    g_object_unref(sourceobj);
}

static void start_next_scan(GTask *task);

static void next_files_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    DtTreeSourceFSScanState *state = g_task_get_task_data(task);
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(g_task_get_source_object(task));
    DtTreeSourceNode *parentNode = g_queue_peek_head(&state->queue);
    GFileEnumerator *fenum = G_FILE_ENUMERATOR(sourceobj);
    GList *files, *nextfile;
    GError *error = NULL;

    GFileInfo **infos = NULL;
    DtTreeSourceNode **childNodes = NULL;
    gint numChildren = 0;
    gint i;

    files = g_file_enumerator_next_files_finish(fenum, res, &error);
    if (files == NULL)
    {
        if (error != NULL)
        {
            // TODO: Should this be fatal? Should I just add a placeholder
            // entry in the tree to indicate that there was an error?
            //g_task_return_error(task, error);
            g_critical("Failed to enumerate files in %s: %s\n",
                    g_file_peek_path(g_file_enumerator_get_container(fenum)),
                    error->message);
            g_clear_error(&error);
        }

        // Close the file enumerator. This shouldn't be cancellable: We might
        // be getting here because the rest of the operation was cancelled, and
        // we don't want to leak anything.
        g_file_enumerator_close_async(fenum, g_task_get_priority(task),
                NULL, file_enum_close_ready, NULL);

        g_queue_pop_head(&state->queue);
        start_next_scan(task);
        return;
    }

    // Scan the list to count the number of files and to create a GFile for
    // each GFileInfo.
    for (nextfile = files; nextfile != NULL; nextfile = nextfile->next)
    {
        GFileInfo *info = G_FILE_INFO(nextfile->data);
        GFile *gf = g_file_enumerator_get_child(fenum, info);
        g_file_info_set_attribute_object(info, DT_FILE_ATTRIBUTE_FS_PATH, G_OBJECT(gf));
        g_object_unref(gf);

        numChildren++;
    }

    // Copy the GFileInfo objects to an array
    infos = g_malloc(numChildren * sizeof(GFileInfo *));
    childNodes = g_malloc(numChildren * sizeof(DtTreeSourceNode *));
    numChildren = 0;
    for (nextfile = files; nextfile != NULL; nextfile = nextfile->next)
    {
        infos[numChildren++] = nextfile->data;
    }

    // Add the files to the tree, and add any new directories to the queue.
    dt_tree_source_base_add_children(DT_TREE_SOURCE_BASE(self), parentNode,
            numChildren, infos, childNodes);
    for (i=0; i<numChildren; i++)
    {
        if (g_file_info_get_file_type(infos[i]) == G_FILE_TYPE_DIRECTORY)
        {
            g_queue_push_tail(&state->queue, childNodes[i]);
        }
    }
    g_list_free_full(files, g_object_unref);
    g_free(infos);
    g_free(childNodes);

    // Start the next batch
    g_file_enumerator_next_files_async(fenum, QUERY_BATCH_SIZE,
                g_task_get_priority(task), g_task_get_cancellable(task),
                next_files_ready, task);
}

static void file_enum_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    //DtTreeSourceFSScanState *state = g_task_get_task_data(task);
    //DtTreeSourceFS *self = DT_TREE_SOURCE_FS(g_task_get_source_object(task));
    //DtTreeSourceNode *node = g_queue_peek_head(&state->queue);
    GError *error = NULL;
    GFileEnumerator *fenum;

    g_debug("Starting file enumeration: %s\n", g_file_peek_path(G_FILE(sourceobj)));
    fenum = g_file_enumerate_children_finish(G_FILE(sourceobj), res, &error);
    if (fenum == NULL)
    {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    g_file_enumerator_next_files_async(fenum, QUERY_BATCH_SIZE,
                g_task_get_priority(task), g_task_get_cancellable(task),
                next_files_ready, task);
}

static void start_next_scan(GTask *task)
{
    DtTreeSourceFSScanState *state = g_task_get_task_data(task);
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(g_task_get_source_object(task));
    DtTreeSourceNode *node = g_queue_peek_head(&state->queue);

    if (node != NULL)
    {
        GFileInfo *info = dt_tree_source_get_file_info(DT_TREE_SOURCE(self), node);
        g_assert(info != NULL);
        g_assert(g_file_info_get_attribute_type(info, DT_FILE_ATTRIBUTE_FS_PATH) == G_FILE_ATTRIBUTE_TYPE_OBJECT);
        GFile *file = G_FILE(g_file_info_get_attribute_object(info, DT_FILE_ATTRIBUTE_FS_PATH));
        GFileQueryInfoFlags flags = G_FILE_QUERY_INFO_NONE;
        if (!self->follow_symlinks)
        {
            flags |= G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
        }

        g_assert(file != NULL);
        g_file_enumerate_children_async(file, FILE_QUERY_ATTRIBS, flags,
                g_task_get_priority(task), g_task_get_cancellable(task),
                file_enum_ready, task);
    }
    else
    {
        // The queue is empty, so we're done.
        g_task_return_boolean(task, TRUE);
        g_object_unref(task);
    }
}

static void scan_root_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    DtTreeSourceFSScanState *state = g_task_get_task_data(task);
    DtTreeSourceFS *self = DT_TREE_SOURCE_FS(g_task_get_source_object(task));
    GError *error = NULL;
    GFileInfo *info;
    DtTreeSourceNode *node;

    info = g_file_query_info_finish(G_FILE(sourceobj), res, &error);
    if (info == NULL)
    {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY)
    {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
                "%s is not a directory", g_file_peek_path(G_FILE(sourceobj)));
        g_object_unref(task);
        return;
    }

    g_file_info_set_attribute_object(info, DT_FILE_ATTRIBUTE_FS_PATH, sourceobj);
    g_file_info_set_name(info, "/");
    g_file_info_set_display_name(info, "/");

    node = dt_tree_source_get_root(DT_TREE_SOURCE(self));
    dt_tree_source_base_set_file_info(DT_TREE_SOURCE_BASE(self), node, info);

    g_queue_push_tail(&state->queue, node);
    start_next_scan(task);
}

static void start_scan_root(GTask *task)
{
    //DtTreeSourceFSScanState *state = g_task_get_task_data(task);
    DtTreeSourceFS *self = g_task_get_source_object(task);
    GFileQueryInfoFlags flags = G_FILE_QUERY_INFO_NONE;
    if (!self->follow_symlinks)
    {
        flags |= G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
    }

    g_file_query_info_async(self->base, FILE_QUERY_ATTRIBS,
            flags, g_task_get_priority(task), g_task_get_cancellable(task),
            scan_root_ready, task);
}

static void cleanup_scan_task_data(gpointer ptr)
{
    if (ptr != NULL)
    {
        DtTreeSourceFSScanState *state = ptr;
        g_queue_clear(&state->queue);
        g_free(state);
    }
}

void dt_tree_source_fs_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata)
{
    GTask *task = g_task_new(self, cancellable, callback, userdata);
    DtTreeSourceFSScanState *state = g_malloc(sizeof(DtTreeSourceFSScanState));
    g_queue_init(&state->queue);

    g_task_set_priority(task, io_priority);
    g_task_set_task_data(task, state, cleanup_scan_task_data);

    start_scan_root(task);
}

gboolean dt_tree_source_fs_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error)
{
    GTask *task = G_TASK(result);
    gboolean ret = g_task_propagate_boolean(task, error);
    return ret;
}

static GFile *lookup_file_for_open(DtTreeSource *self, DtTreeSourceNode *node, GError **error)
{
    GFileInfo *info = dt_tree_source_get_file_info(self, node);
    GFile *gf;

    if (info == NULL)
    {
        g_critical("%s: No file info for row\n", G_STRLOC);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "No file info for row.");
        return NULL;
    }
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
    {
        g_critical("%s: No file info for row\n", G_STRLOC);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                "%s is not a regular file.", g_file_info_get_name(info));
        return NULL;
    }
    gf = G_FILE(g_file_info_get_attribute_object(info, DT_FILE_ATTRIBUTE_FS_PATH));
    g_assert(gf != NULL);

    return gf;
}

static void dt_tree_source_fs_open_file_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    GFileInputStream *stream;
    GError *error = NULL;

    stream = g_file_read_finish(G_FILE(sourceobj), res, &error);
    if (stream != NULL)
    {
        g_task_return_pointer(task, stream, g_object_unref);
    }
    else
    {
        g_task_return_error(task, error);
    }

    // Unreference the task now. The g_task_return_* call above will add a
    // reference until the task's callbck function finishes.
    g_object_unref(task);
}

static void dt_tree_source_fs_open_file_async(DtTreeSource *self, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata)
{
    GError *error = NULL;
    GFile *gf = lookup_file_for_open(self, node, &error);
    if (gf != NULL)
    {
        // Wrap this in a GTask so that the source object to (callback) can
        // point to the DtTreeSourceFS instead of the GFile.
        GTask *task = g_task_new(self, cancellable, callback, userdata);
        g_file_read_async(gf, io_priority, cancellable, dt_tree_source_fs_open_file_ready, task);
    }
    else
    {
        g_task_report_error(self, callback, userdata, NULL, error);
    }
}

static GInputStream *dt_tree_source_fs_open_file_finish(DtTreeSource *self, GAsyncResult *res, GError **error)
{
    GTask *task = G_TASK(res);
    GFileInputStream *stream = g_task_propagate_pointer(task, error);
    if (stream != NULL)
    {
        return G_INPUT_STREAM(stream);
    }
    else
    {
        return NULL;
    }
}

static GInputStream *dt_tree_source_fs_open_file(DtTreeSource *self, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error)
{
    GFile *gf = lookup_file_for_open(self, node, error);
    if (gf != NULL)
    {
        return G_INPUT_STREAM(g_file_read(gf, cancellable, error));
    }
    return NULL;
}
