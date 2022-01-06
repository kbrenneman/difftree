#include "tree-source.h"

#include <assert.h>

G_DEFINE_INTERFACE(DtTreeSource, dt_tree_source, G_TYPE_OBJECT);

enum
{
    SIGNAL_NODES_ADDED,
    SIGNAL_NODES_REMOVED,
    SIGNAL_NODES_CHANGED,
    LAST_SIGNAL
};

static guint tree_source_signals[LAST_SIGNAL] = {};

static void dt_tree_source_default_init(DtTreeSourceInterface *iface)
{
    tree_source_signals[SIGNAL_NODES_ADDED] = g_signal_new("nodes-added",
            DT_TYPE_TREE_SOURCE,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET(DtTreeSourceInterface, nodes_added),
            NULL, NULL, NULL,
            G_TYPE_NONE,
            3, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_POINTER);

    tree_source_signals[SIGNAL_NODES_REMOVED] = g_signal_new("nodes-removed",
            DT_TYPE_TREE_SOURCE,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET(DtTreeSourceInterface, nodes_removed),
            NULL, NULL, NULL,
            G_TYPE_NONE,
            3, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_POINTER);

    tree_source_signals[SIGNAL_NODES_CHANGED] = g_signal_new("nodes-changed",
            DT_TYPE_TREE_SOURCE,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET(DtTreeSourceInterface, nodes_changed),
            NULL, NULL, NULL,
            G_TYPE_NONE,
            4, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_POINTER, G_TYPE_POINTER);
}

DtTreeSourceNode *dt_tree_source_get_root(DtTreeSource *self)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->get_root != NULL, NULL);

    return iface->get_root(self);
}

DtTreeSourceNode *dt_tree_source_get_parent(DtTreeSource *self, DtTreeSourceNode *node)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->get_parent != NULL, NULL);

    return iface->get_parent(self, node);
}

GList *dt_tree_source_get_children(DtTreeSource *self, DtTreeSourceNode *parent)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->get_children != NULL, NULL);

    return iface->get_children(self, parent);
}

DtTreeSourceNode *dt_tree_source_get_child_by_name(DtTreeSource *self, DtTreeSourceNode *parent, const char *name)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);

    if (iface->get_child_by_name != NULL)
    {
        return iface->get_child_by_name(self, parent, name);
    }
    else
    {
        // If the class doesn't implement this, then do a linear search through
        // the child nodes
        GList *children, *ch;
        DtTreeSourceNode *node = NULL;

        g_return_val_if_fail(iface->get_children != NULL, NULL);

        children = iface->get_children(self, parent);
        for (ch = children; ch != NULL; ch = ch->next)
        {
            GFileInfo *info = dt_tree_source_get_file_info(self, (DtTreeSourceNode *) ch->data);
            if (info != NULL)
            {
                const char *chName = g_file_info_get_name(info);
                if (chName != NULL && strcmp(chName, name) == 0)
                {
                    node = ch->data;
                    break;
                }
            }
        }
        g_list_free(children);
        return node;
    }
}

GFileInfo *dt_tree_source_get_file_info(DtTreeSource *self, DtTreeSourceNode *node)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->get_file_info != NULL, NULL);

    return iface->get_file_info(self, node);
}

DtTreeSourceNode **dt_tree_source_get_node_path(DtTreeSource *self, DtTreeSourceNode *node, gint *ret_depth)
{
    DtTreeSourceNode **path;
    DtTreeSourceNode *parent;
    gint depth = 0;
    gint index;

    parent = node;
    while (parent != NULL)
    {
        depth++;
        parent = dt_tree_source_get_parent(self, parent);
    }
    if (ret_depth != NULL)
    {
        *ret_depth = depth;
    }

    path = g_malloc((depth + 1) * sizeof(DtTreeSourceNode *));
    path[depth] = NULL;
    index = depth;
    parent = node;
    while (parent != NULL)
    {
        g_assert(index > 0);
        index--;
        path[index] = parent;
        parent = dt_tree_source_get_parent(self, parent);
    }
    g_assert(index == 0);

    return path;
}

void dt_tree_source_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata)
{
    DtTreeSourceInterface *iface;

    g_return_if_fail(DT_IS_TREE_SOURCE(self));
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_if_fail(iface->scan_async != NULL);

    iface->scan_async(self, io_priority, cancellable, callback, userdata);
}

gboolean dt_tree_source_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), FALSE);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->scan_finish != NULL, FALSE);

    return iface->scan_finish(self, result, error);
}

void dt_tree_source_open_file_async(DtTreeSource *self, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata)
{
    DtTreeSourceInterface *iface;

    g_return_if_fail(DT_IS_TREE_SOURCE(self));
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_if_fail(iface->open_file_async != NULL);

    iface->open_file_async(self, node, io_priority, cancellable, callback, userdata);
}

GInputStream *dt_tree_source_open_file_finish(DtTreeSource *self, GAsyncResult *res, GError **error)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);
    g_return_val_if_fail(iface->open_file_finish != NULL, NULL);

    return iface->open_file_finish(self, res, error);
}

typedef struct
{
    DtTreeSource *source;
    GMainLoop *mainloop;
    GMainContext *maincontext;
    GInputStream *stream;
    GError **error;
} OpenFileAsyncParam;

static void open_file_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    OpenFileAsyncParam *param = userdata;
    param->stream = dt_tree_source_open_file_finish(param->source, res, param->error);
    g_main_loop_quit(param->mainloop);
}

GInputStream *dt_tree_source_open_file_async_wrapper(DtTreeSource *self,
        DtTreeSourceNode *node, GCancellable *cancellable, GError **error)
{
    // Create a new main loop and context, and then call and wait for the
    // async version.
    OpenFileAsyncParam param;
    param.source = self;
    param.maincontext = g_main_context_new();
    param.mainloop = g_main_loop_new(param.maincontext, FALSE);
    param.error = error;
    param.stream = NULL;

    g_main_context_push_thread_default(param.maincontext);
    dt_tree_source_open_file_async(self, node, G_PRIORITY_DEFAULT, cancellable,
            open_file_ready, &param);

    g_main_loop_run(param.mainloop);
    g_main_loop_unref(param.mainloop);
    g_main_context_unref(param.maincontext);
    return param.stream;
}

GInputStream *dt_tree_source_open_file(DtTreeSource *self, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error)
{
    DtTreeSourceInterface *iface;

    g_return_val_if_fail(DT_IS_TREE_SOURCE(self), NULL);
    iface = DT_TREE_SOURCE_GET_IFACE(self);

    if (iface->open_file != NULL)
    {
        return iface->open_file(self, node, cancellable, error);
    }
    else
    {
        return dt_tree_source_open_file_async_wrapper(self, node, cancellable, error);
    }
}

void dt_tree_source_nodes_added(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes)
{
    g_signal_emit(source, tree_source_signals[SIGNAL_NODES_ADDED], 0, parent, num, nodes);
}

void dt_tree_source_nodes_removed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes)
{
    g_signal_emit(source, tree_source_signals[SIGNAL_NODES_REMOVED], 0, parent, num, nodes);
}

void dt_tree_source_nodes_changed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes, GFileInfo **old_info)
{
    g_signal_emit(source, tree_source_signals[SIGNAL_NODES_CHANGED], 0, parent, num, nodes, old_info);
}
