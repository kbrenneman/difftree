#ifndef TREE_SOURCE_H
#define TREE_SOURCE_H

/**
 * \file
 *
 * Defines an interface for a tree of files.
 */

#include <glib.h>
#include <glib-object.h>

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * An opaque struct that contains a single node in the tree.
 */
typedef struct _DtTreeSourceNode DtTreeSourceNode;

/**
 * A GFileInfo attribute used to provide a CRC32 value for a file.
 */
#define DT_FILE_ATTRIBUTE_CRC "dt::crc"

/**
 * A GFileInfo attribute used to provide a filesystem path for a file.
 */
#define DT_FILE_ATTRIBUTE_FS_PATH "dt::fs_path"

#define DT_TYPE_TREE_SOURCE dt_tree_source_get_type()
G_DECLARE_INTERFACE(DtTreeSource, dt_tree_source, DT, TREE_SOURCE, GObject)

struct _DtTreeSourceInterface
{
    GTypeInterface parent_iface;

    DtTreeSourceNode * (* get_root) (DtTreeSource *self);
    DtTreeSourceNode * (* get_parent) (DtTreeSource *self, DtTreeSourceNode *node);
    GList * (* get_children) (DtTreeSource *self, DtTreeSourceNode *parent);
    DtTreeSourceNode * (* get_child_by_name) (DtTreeSource *self, DtTreeSourceNode *parent, const char *name);
    GFileInfo * (* get_file_info) (DtTreeSource *self, DtTreeSourceNode *node);

    void (* scan_async) (DtTreeSource *self, int io_priority,
            GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata);
    gboolean (* scan_finish) (DtTreeSource *self, GAsyncResult *result, GError **error);

    void (* open_file_async) (DtTreeSource *self, DtTreeSourceNode *node,
            int io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer userdata);
    GInputStream * (* open_file_finish) (DtTreeSource *self, GAsyncResult *res, GError **error);

    /**
     * Synchronously opens a file.
     *
     * If this is not implemented, then the default implementation will use
     * open_file_async and an extra main loop.
     */
    GInputStream * (* open_file) (DtTreeSource *self, DtTreeSourceNode *node,
            GCancellable *cancellable, GError **error);

    /* Signals */

    /**
     * Called when nodes are added to the tree.
     */
    void (* nodes_added) (DtTreeSource *source, DtTreeSourceNode *parent,
            gint num_added, DtTreeSourceNode **nodes);

    /**
     * Called when nodes are removed from the tree.
     *
     * The DtSourceNode pointers are still valid during this call for looking
     * up a GFileInfo, but they may or may not appear in the list of children.
     *
     * TODO: Should this take a GFileInfo array instead of a DtTreeSourceNode
     * array?
     */
    void (* nodes_removed) (DtTreeSource *source, DtTreeSourceNode *parent,
            gint num_removed, DtTreeSourceNode **nodes);

    /**
     * Called when nodes the GFileInfo object for one or more nodes has
     * changed.
     */
    void (* nodes_changed) (DtTreeSource *source, DtTreeSourceNode *parent,
            gint num_changed, DtTreeSourceNode **nodes, GFileInfo **old_info);
};

DtTreeSourceNode *dt_tree_source_get_root(DtTreeSource *self);

/**
 * Returns the parent of a node, or NULL if the node is the root.
 */
DtTreeSourceNode *dt_tree_source_get_parent(DtTreeSource *self, DtTreeSourceNode *node);

/**
 * Returns the children of a path.
 *
 * Returns a list of DtTreeSourceNode pointers. The list should be freed by calling:
 * g_list_free().
 *
 * The DtTreeSourceNode objects remain valid as long as they're in the tree.
 */
GList *dt_tree_source_get_children(DtTreeSource *self, DtTreeSourceNode *parent);

/**
 * Returns the DtTreeSourceNode that matches the given name.
 */
DtTreeSourceNode *dt_tree_source_get_child_by_name(DtTreeSource *self, DtTreeSourceNode *parent, const char *name);

/**
 * Returns the GFileInfo object for a given node.
 *
 * \return A GFileInfo object. Transfer none.
 */
GFileInfo *dt_tree_source_get_file_info(DtTreeSource *self, DtTreeSourceNode *node);

/**
 * Returns the full path to a node as an array of DtTreeSourceNode pointers.
 *
 * The first element will be the root node, and the last element will be \p node.
 * The whole array will be terminated with NULL.
 *
 * If \p ret_depth is not NULL, then it returns the length of the array, not
 * including the terminating NULL.
 *
 * The caller must free the array using g_free().
 */
DtTreeSourceNode **dt_tree_source_get_node_path(DtTreeSource *self, DtTreeSourceNode *node, gint *ret_depth);

/**
 * Starts asynchronously populating the DtTreeSoruce.
 *
 * If the DtTreeSource doesn't need to be populated, then the completion
 * callback will still be called.
 */
void dt_tree_source_scan_async(DtTreeSource *self, int io_priority,
        GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata);

/**
 * Finishes a dt_tree_source_scan_async call.
 */
gboolean dt_tree_source_scan_finish(DtTreeSource *self, GAsyncResult *result, GError **error);

/**
 * Opens a file in this source for reading.
 */
void dt_tree_source_open_file_async(DtTreeSource *self, DtTreeSourceNode *node,
        int io_priority, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer userdata);

GInputStream *dt_tree_source_open_file_finish(DtTreeSource *self, GAsyncResult *res, GError **error);

GInputStream *dt_tree_source_open_file(DtTreeSource *self, DtTreeSourceNode *node,
        GCancellable *cancellable, GError **error);

void dt_tree_source_nodes_added(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes);

void dt_tree_source_nodes_removed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes);

void dt_tree_source_nodes_changed(DtTreeSource *source, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes, GFileInfo **old_info);

G_END_DECLS

#endif // TREE_SOURCE_H
