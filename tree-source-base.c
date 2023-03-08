#include "tree-source-base.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef struct _TreeSourceBaseNode
{
    /**
     * A pointer back to the DtTreeSourceBase struct. This is used for basic
     * error-checking to make sure I don't pass a TreeSourceBaseNode to the
     * wrong DtTreeSource.
     */
    DtTreeSourceBase *owner;

    GFileInfo *info;
    struct _TreeSourceBaseNode *parent;

    /**
     * A hashtable to map names to child nodes.
     *
     * \todo Should this be a GHashTable or a GTree?
     */
    GHashTable *children;
} TreeSourceBaseNode;

typedef struct _DtTreeSourceBasePrivate
{
    TreeSourceBaseNode *root;
} DtTreeSourceBasePrivate;

static void dt_tree_source_base_interface_init(DtTreeSourceInterface *iface);

static DtTreeSourceNode *dt_tree_source_base_get_root(DtTreeSource *self);
static DtTreeSourceNode *dt_tree_source_base_get_parent(DtTreeSource *self, DtTreeSourceNode *node);
static GList *dt_tree_source_base_get_children(DtTreeSource *self, DtTreeSourceNode *parent);
static DtTreeSourceNode *dt_tree_source_base_get_child_by_name(DtTreeSource *self, DtTreeSourceNode *parent, const char *name);
static GFileInfo *dt_tree_source_base_get_file_info(DtTreeSource *self, DtTreeSourceNode *node);
static void dt_tree_source_base_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata);
static gboolean dt_tree_source_base_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error);

/**
 * Checks whether a node is valid.
 */
static TreeSourceBaseNode *check_node(DtTreeSourceBase *self, DtTreeSourceNode *snode);

static TreeSourceBaseNode *node_create(DtTreeSourceBase *self, GFileInfo *info);
static void node_free(TreeSourceBaseNode *node);
static void node_add_child(TreeSourceBaseNode *parent, TreeSourceBaseNode *child);
static void node_detach(TreeSourceBaseNode *node);

G_DEFINE_TYPE_WITH_CODE(DtTreeSourceBase, dt_tree_source_base, G_TYPE_OBJECT,
        G_ADD_PRIVATE(DtTreeSourceBase)
        G_IMPLEMENT_INTERFACE(DT_TYPE_TREE_SOURCE, dt_tree_source_base_interface_init));

#define GET_PRIVATE(inst) dt_tree_source_base_get_instance_private(DT_TREE_SOURCE_BASE(inst))

static void dt_tree_source_base_dispose(GObject *gobj)
{
    // This is where I unref any internal objects. We might be called more than
    // once, so use g_clear_object to set any pointers to NULL.
    //DtTreeSourceBase *self = DT_TREE_SOURCE_BASE(gobj);

    // Chain up to GObject.
    G_OBJECT_CLASS(dt_tree_source_base_parent_class)->dispose(gobj);
}

static void dt_tree_source_base_finalize(GObject *gobj)
{
    // Free anything that we didn't free in dispose().
    DtTreeSourceBase *self = DT_TREE_SOURCE_BASE(gobj);
    DtTreeSourceBasePrivate *priv = GET_PRIVATE(self);
    node_free(priv->root);
    G_OBJECT_CLASS(dt_tree_source_base_parent_class)->finalize(gobj);
}

static void dt_tree_source_base_interface_init(DtTreeSourceInterface *iface)
{
    iface->get_root = dt_tree_source_base_get_root;
    iface->get_parent = dt_tree_source_base_get_parent;
    iface->get_children = dt_tree_source_base_get_children;
    iface->get_child_by_name = dt_tree_source_base_get_child_by_name;
    iface->get_file_info = dt_tree_source_base_get_file_info;
    iface->scan_async = dt_tree_source_base_scan_async;
    iface->scan_finish = dt_tree_source_base_scan_finish;

    // open_file, open_file_async, and open_file_finish are not implemented
    // here.
}

static void dt_tree_source_base_class_init(DtTreeSourceBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = dt_tree_source_base_dispose;
    object_class->finalize = dt_tree_source_base_finalize;
}

static void dt_tree_source_base_init(DtTreeSourceBase *self)
{
    DtTreeSourceBasePrivate *priv = GET_PRIVATE(self);
    GFileInfo *info = g_file_info_new();

    g_file_info_set_name(info, "/");
    g_file_info_set_file_type(info, G_FILE_TYPE_DIRECTORY);
    priv->root = node_create(self, info);
}

static TreeSourceBaseNode *check_node(DtTreeSourceBase *self, DtTreeSourceNode *snode)
{
    if (snode != NULL)
    {
        TreeSourceBaseNode *node = (TreeSourceBaseNode *) snode;
        if (node->owner == self)
        {
            return node;
        }
        else
        {
            g_critical("Invalid node");
            return NULL;
        }
    }
    return NULL;
}

static TreeSourceBaseNode *node_create(DtTreeSourceBase *self, GFileInfo *info)
{
    TreeSourceBaseNode *node = g_malloc(sizeof(TreeSourceBaseNode));
    node->owner = self;
    node->info = info;
    node->parent = NULL;
    node->children = NULL;

    return node;
}

static void node_free(TreeSourceBaseNode *node)
{
    if (node != NULL)
    {
        if (node->children != NULL)
        {
            g_hash_table_destroy(node->children);
        }

        if (node->info != NULL)
        {
            g_object_unref(node->info);
        }
        g_free(node);
    }
}

static void node_add_child(TreeSourceBaseNode *parent, TreeSourceBaseNode *child)
{
    const gchar *name = g_file_info_get_name(child->info);
    g_return_if_fail(name != NULL);

    if (parent->children == NULL)
    {
        parent->children = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, (GDestroyNotify) node_free);
    }
    if (!g_hash_table_replace(parent->children, g_strdup(name), child))
    {
        g_critical("Duplicate hash table key: \"%s\"\n", name);
    }
    child->parent = parent;
}

static void node_detach(TreeSourceBaseNode *node)
{
    if (node->parent != NULL)
    {
        gchar *key = NULL;
        TreeSourceBaseNode *found = NULL;

        g_assert(node->parent->children != NULL);

        if (g_hash_table_lookup_extended(node->parent->children,
                    g_file_info_get_name(node->info),
                    (void **) &key, (void **) found))
        {
            g_assert(node == found);
            g_hash_table_steal(node->parent->children, g_file_info_get_name(node->info));
            g_free(key);
            node->parent = NULL;
        }
        else
        {
            g_error("Can't happen: Node not in parent's children hashtable");
        }
    }
}

static DtTreeSourceNode *dt_tree_source_base_get_root(DtTreeSource *self)
{
    DtTreeSourceBasePrivate *priv = GET_PRIVATE(self);
    return (DtTreeSourceNode *) priv->root;
}

static DtTreeSourceNode *dt_tree_source_base_get_parent(DtTreeSource *self, DtTreeSourceNode *inode)
{
    TreeSourceBaseNode *node = check_node(DT_TREE_SOURCE_BASE(self), inode);
    g_return_val_if_fail(node != NULL, NULL);
    return (DtTreeSourceNode *) node->parent;
}

static GList *dt_tree_source_base_get_children(DtTreeSource *self, DtTreeSourceNode *iparent)
{
    TreeSourceBaseNode *parent = check_node(DT_TREE_SOURCE_BASE(self), iparent);

    g_return_val_if_fail(parent != NULL, NULL);

    if (parent->children != NULL)
    {
        return g_hash_table_get_values(parent->children);
    }
    else
    {
        return NULL;
    }
}

static DtTreeSourceNode *dt_tree_source_base_get_child_by_name(DtTreeSource *self, DtTreeSourceNode *iparent, const char *name)
{
    TreeSourceBaseNode *parent = check_node(DT_TREE_SOURCE_BASE(self), iparent);

    g_return_val_if_fail(parent != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);
    if (parent->children != NULL)
    {
        return g_hash_table_lookup(parent->children, name);
    }
    else
    {
        return NULL;
    }
}

static GFileInfo *dt_tree_source_base_get_file_info(DtTreeSource *self, DtTreeSourceNode *inode)
{
    TreeSourceBaseNode *node = check_node(DT_TREE_SOURCE_BASE(self), inode);

    g_return_val_if_fail(node != NULL, NULL);
    return node->info;
}

void dt_tree_source_base_add_children(DtTreeSourceBase *self, DtTreeSourceNode *iparent,
        gint num, GFileInfo **info, DtTreeSourceNode **ret_nodes)
{
    TreeSourceBaseNode *parent = check_node(self, iparent);
    DtTreeSourceNode **new_nodes;
    gint i;

    g_return_if_fail(parent != NULL);

    if (ret_nodes != NULL)
    {
        new_nodes = ret_nodes;
    }
    else
    {
        new_nodes = g_malloc(num * sizeof(DtTreeSourceNode *));
    }

    for (i=0; i<num; i++)
    {
        TreeSourceBaseNode *child;
        g_assert(info[i] != NULL);
        g_assert(G_IS_FILE_INFO(info[i]));

        child = node_create(self, g_object_ref(info[i]));
        node_add_child(parent, child);
        new_nodes[i] = (DtTreeSourceNode *) child;
    }

    dt_tree_source_nodes_added(DT_TREE_SOURCE(self),
            iparent, num, new_nodes);

    if (ret_nodes == NULL)
    {
        g_free(new_nodes);
    }
}

void dt_tree_source_base_remove_children(DtTreeSourceBase *self, DtTreeSourceNode *iparent,
        gint num, DtTreeSourceNode **nodes)
{
    TreeSourceBaseNode *parent = check_node(self, iparent);
    gint i;

    g_return_if_fail(parent != NULL);

    if (num == 0)
    {
        return;
    }

    for (i=0; i<num; i++)
    {
        TreeSourceBaseNode *child = check_node(self, nodes[i]);
        if (child == NULL || child->parent != parent)
        {
            g_error("Invalid child node\n");
            continue;
        }
        node_detach(child);
    }

    dt_tree_source_nodes_removed(DT_TREE_SOURCE(self), iparent, num, nodes);
}

void dt_tree_source_base_set_file_info(DtTreeSourceBase *self, DtTreeSourceNode *inode, GFileInfo *info)
{
    TreeSourceBaseNode *node = check_node(self, inode);
    GFileInfo *oldInfo = NULL;

    g_return_if_fail(node != NULL);
    g_return_if_fail(info != NULL);
    g_assert(strcmp(g_file_info_get_name(info), g_file_info_get_name(node->info)) == 0);

    oldInfo = g_object_ref(node->info);
    g_set_object(&node->info, info);

    dt_tree_source_nodes_changed(DT_TREE_SOURCE(self), (DtTreeSourceNode *) node->parent,
            1, &inode, &oldInfo);
    g_object_unref(oldInfo);
}

void dt_tree_source_base_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata)
{
    // As a default implementation, just queue the completion callback.
    GTask *task = g_task_new(self, cancellable, callback, userdata);
    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}

gboolean dt_tree_source_base_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error)
{
    GTask *task = G_TASK(result);
    return g_task_propagate_boolean(task, error);
}

