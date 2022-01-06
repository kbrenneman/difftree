#include "diff-tree-model.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <gio/gio.h>

static const gint64 DEFAULT_MAX_READ_SIZE = (16 * 1024 * 1024);
#define READ_BLOCK_SIZE 4096

struct _DtDiffTreeModel
{
    GtkTreeStore parent_instance;

    gint num_sources;
    DtTreeSource **sources;
    gint64 max_read_size;
};

G_DEFINE_TYPE(DtDiffTreeModel, dt_diff_tree_model, GTK_TYPE_TREE_STORE);

enum
{
    PROP_MAX_READ_SIZE = 1,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {};

static void dt_diff_tree_model_dispose(GObject *gobj)
{
    //DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(gobj);
    // TODO: We should clear the references to the DtTreeSource objects here,
    // not in dt_diff_tree_model_finalize.

    // Chain up to GObject.
    G_OBJECT_CLASS(dt_diff_tree_model_parent_class)->dispose(gobj);
}

static void dt_diff_tree_model_finalize(GObject *gobj)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(gobj);
    gint i;
    for (i=0; i<self->num_sources; i++)
    {
        g_signal_handlers_disconnect_by_data(self->sources[i], self);
        g_object_unref(self->sources[i]);
    }
    g_free(self->sources);
    G_OBJECT_CLASS(dt_diff_tree_model_parent_class)->finalize(gobj);
}

static void dt_diff_tree_model_init(DtDiffTreeModel *self)
{
    self->num_sources = 0;
    self->sources = NULL;
    self->max_read_size = DEFAULT_MAX_READ_SIZE;
}

static void dt_diff_tree_model_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(object);
    switch (property_id)
    {
        case PROP_MAX_READ_SIZE:
            self->max_read_size = g_value_get_int64(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void dt_diff_tree_model_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(object);
    switch (property_id)
    {
        case PROP_MAX_READ_SIZE:
            g_value_set_int64(value, self->max_read_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void dt_diff_tree_model_class_init(DtDiffTreeModelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = dt_diff_tree_model_set_property;
    object_class->get_property = dt_diff_tree_model_get_property;
    obj_properties[PROP_MAX_READ_SIZE] = g_param_spec_int64(
            "max-read-size",
            "Maximum read size",
            "Maximum size of a file to read to check for differences",
            -1, G_MAXINT64, DEFAULT_MAX_READ_SIZE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);

    object_class->dispose = dt_diff_tree_model_dispose;
    object_class->finalize = dt_diff_tree_model_finalize;
}

static gboolean find_child_iter(DtDiffTreeModel *self, GtkTreeIter *iter,
        GtkTreeIter *parent, const gchar *name, GFileType type)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self);
    GtkTreeIter child;
    gboolean ok;
    for (ok = gtk_tree_model_iter_children(model, &child, parent);
            ok;
            ok = gtk_tree_model_iter_next(model, &child))
    {
        gchar *childName = NULL;
        GFileType childType = G_FILE_TYPE_UNKNOWN;
        gtk_tree_model_get(GTK_TREE_MODEL(self), &child,
                DT_DIFF_TREE_MODEL_COL_NAME, &childName,
                DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &childType, -1);

        if (childName == NULL)
        {
            g_critical("Shouldn't happen: name is NULL");
            continue;
        }
        if (childType == type && strcmp(name, childName) == 0)
        {
            g_free(childName);
            *iter = child;
            return TRUE;
        }
        g_free(childName);
    }

    return FALSE;
}

static void add_child_node(DtDiffTreeModel *self, GtkTreeIter *iter,
        GtkTreeIter *parent, gint source_index, DtTreeSourceNode *node)
{
    GtkTreeIter child;
    GPtrArray *nodeArray;
    DtTreeSource *source = self->sources[source_index];
    GFileInfo *info = dt_tree_source_get_file_info(source, node);

    nodeArray = g_ptr_array_new();
    g_ptr_array_set_size(nodeArray, self->num_sources);
    nodeArray->pdata[source_index] = node;

    gtk_tree_store_insert_with_values(GTK_TREE_STORE(self), &child, parent, 0,
            DT_DIFF_TREE_MODEL_COL_NAME, g_file_info_get_name(info),
            DT_DIFF_TREE_MODEL_COL_DIFFERENT, DT_DIFF_TYPE_DIFFERENT,
            DT_DIFF_TREE_MODEL_COL_FILE_TYPE, g_file_info_get_file_type(info),
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, nodeArray, -1);
    *iter = child;
}

/**
 * Checks for differences based on the GFileInfo objects from each source.
 *
 * This basically checks everything that we can without actually reading the
 * files.
 */
static DtDiffType check_file_diff_basic(gint num_sources, GFileInfo **infos)
{
    gint i;

    // Check if the file exists in every source.
    for (i=0; i<num_sources; i++)
    {
        if (infos[i] == NULL)
        {
            // The file is missing from at least one source.
            return DT_DIFF_TYPE_DIFFERENT;
        }

        g_assert(g_file_info_get_file_type(infos[0]) == g_file_info_get_file_type(infos[i]));
        g_assert(strcmp(g_file_info_get_name(infos[0]), g_file_info_get_name(infos[i])) == 0);
    }

    if (g_file_info_get_file_type(infos[0]) == G_FILE_TYPE_DIRECTORY)
    {
        // We don't bother comparing directories.
        return DT_DIFF_TYPE_IDENTICAL;
    }
    else if (g_file_info_get_file_type(infos[0]) == G_FILE_TYPE_REGULAR)
    {
        guint32 firstCRC = 0;
        gboolean anyCRC = FALSE;
        gboolean allCRC = TRUE;

        for (i=0; i<num_sources; i++)
        {
            if (g_file_info_get_size(infos[0]) != g_file_info_get_size(infos[i]))
            {
                return DT_DIFF_TYPE_DIFFERENT;
            }

            if (g_file_info_has_attribute(infos[i], DT_FILE_ATTRIBUTE_CRC))
            {
                if (!anyCRC)
                {
                    anyCRC = TRUE;
                    firstCRC = g_file_info_get_attribute_uint32(infos[i], DT_FILE_ATTRIBUTE_CRC);
                }
                else if (firstCRC != g_file_info_get_attribute_uint32(infos[i], DT_FILE_ATTRIBUTE_CRC))
                {
                    return DT_DIFF_TYPE_DIFFERENT;
                }
            }
            else
            {
                allCRC = FALSE;
            }
        }

        if (allCRC)
        {
            // Every file had a CRC, and they all matched.
            return DT_DIFF_TYPE_IDENTICAL;
        }

        // We'll have to read the files to determine if they're different or not.
        return DT_DIFF_TYPE_UNKNOWN;
    }
    else if (g_file_info_get_file_type(infos[0]) == G_FILE_TYPE_SYMBOLIC_LINK)
    {
        // For a symlink, check the target
        const char *first_target = g_file_info_get_symlink_target(infos[0]);
        for (i=1; i<num_sources; i++)
        {
            const char *target = g_file_info_get_symlink_target(infos[1]);
            if (g_strcmp0(target, first_target) != 0)
            {
                return DT_DIFF_TYPE_DIFFERENT;
            }
        }
        return DT_DIFF_TYPE_IDENTICAL;
    }
    else
    {
        // Ignore anything other than a regular file for now.
        return DT_DIFF_TYPE_UNKNOWN;
    }
}

static void update_diff_type(DtDiffTreeModel *self, GtkTreeIter *iter)
{
    GPtrArray *nodeArray = NULL;
    GFileInfo **infos;
    DtDiffType diff;
    gint i;

    gtk_tree_model_get(GTK_TREE_MODEL(self), iter,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);
    infos = g_malloc(self->num_sources * sizeof(GFileInfo *));
    for (i=0; i<self->num_sources; i++)
    {
        if (nodeArray->pdata[i] != NULL)
        {
            infos[i] = dt_tree_source_get_file_info(self->sources[i], nodeArray->pdata[i]);
        }
        else
        {
            infos[i] = NULL;
        }
    }
    diff = check_file_diff_basic(self->num_sources, infos);
    g_free(infos);
    g_ptr_array_unref(nodeArray);

    gtk_tree_store_set(GTK_TREE_STORE(self), iter, DT_DIFF_TREE_MODEL_COL_DIFFERENT, diff, -1);
}

/**
 * Adds or updates a row with a new DtTreeSourceNode.
 */
static void add_source_node(DtDiffTreeModel *self, GtkTreeIter *parent,
        gint source_index, DtTreeSourceNode *node)
{
    GtkTreeIter child;
    GFileInfo *info = dt_tree_source_get_file_info(self->sources[source_index], node);

    if (find_child_iter(self, &child, parent, g_file_info_get_name(info),
                g_file_info_get_file_type(info)))
    {
        GPtrArray *nodeArray = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(self), &child,
                DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);
        nodeArray->pdata[source_index] = node;
        g_ptr_array_unref(nodeArray);

        // Update the diff type and send out a row-changed event
        update_diff_type(self, &child);
    }
    else
    {
        add_child_node(self, &child, parent, source_index, node);
    }
}

static void remove_source_node(DtDiffTreeModel *self, GtkTreeIter *parent,
        gint source_index, DtTreeSourceNode *node)
{
    GtkTreeIter child;
    GFileInfo *info = dt_tree_source_get_file_info(self->sources[source_index], node);

    if (find_child_iter(self, &child, parent, g_file_info_get_name(info),
                g_file_info_get_file_type(info)))
    {
        GPtrArray *nodeArray = NULL;
        gboolean keep = FALSE;
        gint i;

        gtk_tree_model_get(GTK_TREE_MODEL(self), &child,
                DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);
        nodeArray->pdata[source_index] = NULL;
        for (i=0; i<self->num_sources; i++)
        {
            if (nodeArray->pdata[i] != NULL)
            {
                keep = TRUE;
                break;
            }
        }
        g_ptr_array_unref(nodeArray);

        if (keep)
        {
            update_diff_type(self, &child);
        }
        else
        {
            g_assert(!gtk_tree_model_iter_has_child(GTK_TREE_MODEL(self), &child));
            gtk_tree_store_remove(GTK_TREE_STORE(self), &child);
        }
    }
}

static void init_tree(DtDiffTreeModel *self, GtkTreeIter *parent)
{
    GPtrArray *nodeArray = NULL;
    GtkTreeIter childIter;
    gboolean ok;
    gint i;

    gtk_tree_model_get(GTK_TREE_MODEL(self), parent,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);

    // TODO: Rewrite this to be more efficient? I can collect a map from (name,
    // type) to the child nodes, then add each node once. If the model always
    // starts with just the root node, though, then it won't matter.
    for (i=0; i<self->num_sources; i++)
    {
        if (nodeArray->pdata[i] != NULL)
        {
            GList *children = dt_tree_source_get_children(self->sources[i], nodeArray->pdata[i]);
            GList *ch;
            for (ch = children; ch != NULL; ch = ch->next)
            {
                add_source_node(self, parent, i, ch->data);
            }
            g_list_free(children);
        }
    }
    g_ptr_array_unref(nodeArray);

    for (ok = gtk_tree_model_iter_children(GTK_TREE_MODEL(self), &childIter, parent);
            ok;
            ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(self), &childIter))
    {
        init_tree(self, &childIter);
    }
}

static void lookup_source_node(DtDiffTreeModel *self, DtTreeSource *source,
        DtTreeSourceNode *node, GtkTreeIter *iter)
{
    gint i;

    g_assert(node != NULL);

    if (iter != NULL)
    {
        gint depth = -1;
        DtTreeSourceNode **path = dt_tree_source_get_node_path(source, node, &depth);
        if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(self), iter, NULL))
        {
            g_error("No root node");
        }

        g_assert(depth > 0);

        for (i=1; path[i] != NULL; i++)
        {
            GFileInfo *info = dt_tree_source_get_file_info(source, path[i]);
            if (!find_child_iter(self, iter, iter, g_file_info_get_name(info), g_file_info_get_file_type(info)))
            {
                g_error("Can't find child node");
            }
        }
        g_free(path);
    }
}

static gint lookup_source_index(DtDiffTreeModel *self, DtTreeSource *source)
{
    gint i;
    for (i=0; i<self->num_sources; i++)
    {
        if (source == self->sources[i])
        {
            return i;
        }
    }
    g_error("Invalid DtTreeSource\n");
}

static void on_source_nodes_added(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num_added, DtTreeSourceNode **nodes, gpointer userdata)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(userdata);
    GtkTreeIter parentIter;
    gint source_index = -1;
    gint i;

    lookup_source_node(self, source, parent, &parentIter);
    source_index = lookup_source_index(self, source);

    for (i=0; i<num_added; i++)
    {
        add_source_node(self, &parentIter, source_index, nodes[i]);
    }
}

static void on_source_nodes_removed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num_removed, DtTreeSourceNode **nodes, gpointer userdata)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(userdata);
    GtkTreeIter parentIter;
    gint source_index = -1;
    gint i;

    lookup_source_node(self, source, parent, &parentIter);
    source_index = lookup_source_index(self, source);

    for (i=0; i<num_removed; i++)
    {
        remove_source_node(self, &parentIter, source_index, nodes[i]);
    }
}

static void on_source_nodes_changed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num_changed, DtTreeSourceNode **nodes, GFileInfo **old_info, gpointer userdata)
{
    DtDiffTreeModel *self = DT_DIFF_TREE_MODEL(userdata);
    GtkTreeIter parentIterBuf;
    GtkTreeIter *parentIter;
    gint source_index = -1;
    gint i;

    if (parent != NULL)
    {
        parentIter = &parentIterBuf;
        lookup_source_node(self, source, parent, parentIter);
    }
    else
    {
        parentIter = NULL;
    }
    source_index = lookup_source_index(self, source);

    for (i=0; i<num_changed; i++)
    {
        add_source_node(self, parentIter, source_index, nodes[i]);
    }
}

DtDiffTreeModel *dt_diff_tree_model_new(gint num_sources, DtTreeSource **sources)
{
    GType column_types[DT_DIFF_TREE_MODEL_NUM_COLUMNS] =
    {
        G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_INT,
        G_TYPE_PTR_ARRAY
    };
    DtDiffTreeModel *self = g_object_new(DT_TYPE_DIFF_TREE_MODEL, NULL);
    GtkTreeIter root;
    GPtrArray *nodeArray;
    gint i;

    gtk_tree_store_set_column_types(GTK_TREE_STORE(self), DT_DIFF_TREE_MODEL_NUM_COLUMNS, column_types);

    self->num_sources = num_sources;
    self->sources = g_malloc(num_sources * sizeof(DtTreeSource *));
    for (i=0; i<num_sources; i++)
    {
        self->sources[i] = g_object_ref(sources[i]);
    }

    // Add the root node.
    nodeArray = g_ptr_array_new();
    g_ptr_array_set_size(nodeArray, num_sources);
    for (i=0; i<num_sources; i++)
    {
        nodeArray->pdata[i] = dt_tree_source_get_root(sources[i]);
    }
    gtk_tree_store_insert_with_values(GTK_TREE_STORE(self), &root, NULL, 0,
            DT_DIFF_TREE_MODEL_COL_NAME, "/",
            DT_DIFF_TREE_MODEL_COL_DIFFERENT, DT_DIFF_TYPE_IDENTICAL,
            DT_DIFF_TREE_MODEL_COL_FILE_TYPE, G_FILE_TYPE_DIRECTORY,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, nodeArray,
            -1);
    g_ptr_array_unref(nodeArray);

    // Initialize the rest of the tree
    init_tree(self, &root);

    for (i=0; i<num_sources; i++)
    {
        g_signal_connect(sources[i], "nodes-added", G_CALLBACK(on_source_nodes_added), self);
        g_signal_connect(sources[i], "nodes-removed", G_CALLBACK(on_source_nodes_removed), self);
        g_signal_connect(sources[i], "nodes-changed", G_CALLBACK(on_source_nodes_changed), self);
    }

    return self;
}

gint dt_diff_tree_model_get_num_sources(DtDiffTreeModel *self)
{
    return self->num_sources;
}

DtTreeSource *dt_diff_tree_model_get_source(DtDiffTreeModel *self, gint source_index)
{
    g_return_val_if_fail(source_index >= 0, NULL);
    g_return_val_if_fail(source_index < self->num_sources, NULL);
    return self->sources[source_index];
}

DtTreeSourceNode *dt_diff_tree_model_get_source_node(DtDiffTreeModel *self,
        gint source_index, GtkTreeIter *iter)
{
    GPtrArray *nodeArray = NULL;
    DtTreeSourceNode *node = NULL;

    g_return_val_if_fail(source_index >= 0, NULL);
    g_return_val_if_fail(source_index < self->num_sources, NULL);

    gtk_tree_model_get(GTK_TREE_MODEL(self), iter,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);
    g_return_val_if_fail(nodeArray != NULL, NULL);
    node = nodeArray->pdata[source_index];
    g_ptr_array_unref(nodeArray);
    return node;
}

typedef struct
{
    GInputStream *stream;
} CheckDiffSourceState;

typedef struct
{
    DtDiffTreeModel *model;
    GtkTreeRowReference *row;
    gboolean aborted;

    CheckDiffSourceState *sources;

    gchar buffer0[READ_BLOCK_SIZE];
    gchar buffer1[READ_BLOCK_SIZE];
    gsize numread;

    gint pending_source;
} CheckDiffState;

void cleanup_check_diff_state(gpointer ptr)
{
    if (ptr != NULL)
    {
        CheckDiffState *state = ptr;
        gint i;

        if (state->sources != NULL)
        {
            for (i=0; i<state->model->num_sources; i++)
            {
                g_clear_object(&state->sources[i].stream);
            }
            g_free(state->sources);
        }

        gtk_tree_row_reference_free(state->row);
        g_clear_object(&state->model);
        g_free(state);
    }
}

static void check_diff_start_next_open(GTask *task, gint source_index);
static void check_diff_start_next_read(GTask *task, gint source_index);

void on_check_diff_open_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    CheckDiffState *state = g_task_get_task_data(task);
    GError *error = NULL;
    gint source_index = state->pending_source;

    g_assert(state->model->sources[source_index] == DT_TREE_SOURCE(sourceobj));

    state->sources[source_index].stream = dt_tree_source_open_file_finish(DT_TREE_SOURCE(sourceobj), res, &error);
    if (state->sources[source_index].stream == NULL)
    {
        g_task_return_error(task, error);
        return;
    }

    if (source_index + 1 < state->model->num_sources)
    {
        check_diff_start_next_open(task, source_index + 1);
    }
    else
    {
        check_diff_start_next_read(task, 0);
    }
}

void on_check_diff_read_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    GTask *task = G_TASK(userdata);
    CheckDiffState *state = g_task_get_task_data(task);
    gint source_index = state->pending_source;
    GError *error = NULL;
    gsize num = 0;

    if (!g_input_stream_read_all_finish(G_INPUT_STREAM(sourceobj), res, &num, &error))
    {
        g_task_return_error(task, error);
        return;
    }

    if (source_index == 0)
    {
        state->numread = num;
    }
    else
    {
        if (state->numread != num)
        {
            g_task_return_int(task, DT_DIFF_TYPE_DIFFERENT);
            return;
        }
        if (memcmp(state->buffer0, state->buffer1, num) != 0)
        {
            g_task_return_int(task, DT_DIFF_TYPE_DIFFERENT);
            return;
        }
    }

    if (source_index + 1 < state->model->num_sources)
    {
        check_diff_start_next_read(task, source_index + 1);
    }
    else
    {
        // We got to the last source.
        if (num == 0)
        {
            // We read to the end of the file with no differences
            g_task_return_int(task, DT_DIFF_TYPE_IDENTICAL);
            return;
        }
        else
        {
            // Start reading the next block
            check_diff_start_next_read(task, 0);
        }
    }
}

static void check_diff_start_next_read(GTask *task, gint source_index)
{
    CheckDiffState *state = g_task_get_task_data(task);
    void *buffer;

    if (source_index == 0)
    {
        buffer = state->buffer0;
    }
    else
    {
        buffer = state->buffer1;
    }

    state->pending_source = source_index;
    g_input_stream_read_all_async(state->sources[source_index].stream,
            buffer, READ_BLOCK_SIZE,
            g_task_get_priority(task), g_task_get_cancellable(task),
            on_check_diff_read_ready, task);
}

static void check_diff_start_next_open(GTask *task, gint source_index)
{
    CheckDiffState *state = g_task_get_task_data(task);
    GPtrArray *nodeArray = NULL;
    GtkTreePath *path;
    GtkTreeIter iter;

    path = gtk_tree_row_reference_get_path(state->row);
    if (path == NULL)
    {
        // TODO: Pick a better error code for this
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                "File was removed from source");
        return;
    }
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(state->model), &iter, path))
    {
        gtk_tree_path_free(path);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                "File was removed from source");
        return;
    }
    gtk_tree_path_free(path);

    gtk_tree_model_get(GTK_TREE_MODEL(state->model), &iter,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodeArray, -1);

    if (nodeArray->pdata[source_index] == NULL)
    {
        g_task_return_int(task, DT_DIFF_TYPE_DIFFERENT);
        g_ptr_array_unref(nodeArray);
        return;
    }

    state->pending_source = source_index;
    dt_tree_source_open_file_async(state->model->sources[source_index],
            nodeArray->pdata[source_index], g_task_get_priority(task),
            g_task_get_cancellable(task), on_check_diff_open_ready, task);
}

static gboolean check_diff_can_run(DtDiffTreeModel *self, GtkTreeIter *iter)
{
    DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;
    DtTreeSourceNode *node;
    GFileInfo *info;

    gtk_tree_model_get(GTK_TREE_MODEL(self), iter,
            DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff, -1);
    if (diff != DT_DIFF_TYPE_UNKNOWN)
    {
        return FALSE;
    }

    node = dt_diff_tree_model_get_source_node(self, 0, iter);
    if (node == NULL)
    {
        // This shouldn't happen. If this file is missing from any source, then
        // we should have already set the diff column to DT_DIFF_TYPE_DIFFERENT.
        g_critical("Can't find source node from dt_diff_tree_model_check_difference_async");
        return FALSE;
    }

    info = dt_tree_source_get_file_info(self->sources[0], node);
    if (g_file_info_get_size(info) > self->max_read_size)
    {
        // This file is bigger than we're willing to read
        return FALSE;
    }

    return TRUE;
}

void dt_diff_tree_model_check_difference_async(DtDiffTreeModel *self,
        GtkTreeIter *iter, gint io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata)
{
    GTask *task;
    CheckDiffState *state;
    GtkTreePath *path;
    gint i;

    if (!check_diff_can_run(self, iter))
    {
        // Create a GTask but immediately return.
        task = g_task_new(self, cancellable, callback, userdata);
        g_task_return_int(task, DT_DIFF_TYPE_UNKNOWN);
        return;
    }

    if (cancellable == NULL)
    {
        // We need a GCancellable internally, so create one if the caller
        // didn't provide one.
        cancellable = g_cancellable_new();
    }
    else
    {
        g_object_ref(cancellable);
    }
    task = g_task_new(self, cancellable, callback, userdata);
    g_task_set_priority(task, io_priority);

    state = g_malloc0(sizeof(CheckDiffState));
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(self), iter);

    state->model = g_object_ref(self);
    state->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(self), path);
    gtk_tree_path_free(path);

    state->sources = g_malloc(self->num_sources * sizeof(CheckDiffSourceState));
    for (i=0; i<self->num_sources; i++)
    {
        state->sources[i].stream = NULL;
    }
    g_task_set_task_data(task, state, cleanup_check_diff_state);

    // TODO: Abort the check if the file changes or is removed while it's
    // running. For this to work, I have to make sure that the row-changed and
    // row-deleted callbacks get disconnected when the GTask is destroyed.

    check_diff_start_next_open(task, 0);
    g_object_unref(cancellable);
}

gboolean dt_diff_tree_model_check_difference_finish(DtDiffTreeModel *self, GAsyncResult *res, GError **error)
{
    GTask *task = G_TASK(res);
    CheckDiffState *state = g_task_get_task_data(task);
    gssize result = g_task_propagate_int(task, error);
    gboolean ret;

    if (state != NULL && state->aborted)
    {
        // If we aborted the operation because the row changed, then treat this
        // as a success, but don't change the model.
        g_clear_error(error);
        return TRUE;
    }

    if (result >= 0)
    {
        if (state != NULL)
        {
            g_assert(result == DT_DIFF_TYPE_IDENTICAL || result == DT_DIFF_TYPE_DIFFERENT);
            GtkTreePath *path = gtk_tree_row_reference_get_path(state->row);
            GtkTreeIter iter;

            g_signal_handlers_disconnect_by_data(self, task);

            if (path != NULL)
            {
                if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self), &iter, path))
                {
                    DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;
                    gtk_tree_model_get(GTK_TREE_MODEL(self), &iter,
                            DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff, -1);
                    if (diff == DT_DIFF_TYPE_UNKNOWN)
                    {
                        gtk_tree_store_set(GTK_TREE_STORE(self), &iter,
                                DT_DIFF_TREE_MODEL_COL_DIFFERENT, (DtDiffType) result, -1);
                    }
                }
                gtk_tree_path_free(path);
            }
        }
        else
        {
            g_assert(result == DT_DIFF_TYPE_UNKNOWN);
        }
        ret = TRUE;
    }
    else
    {
        ret = FALSE;
    }

    g_object_unref(task);
    return ret;
}
