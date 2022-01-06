#ifndef DIFF_TREE_MODEL_H
#define DIFF_TREE_MODEL_H

#include <gtk/gtk.h>

#include "tree-source.h"

G_BEGIN_DECLS

typedef enum
{
    DT_DIFF_TYPE_UNKNOWN,
    DT_DIFF_TYPE_IDENTICAL,
    DT_DIFF_TYPE_DIFFERENT,
} DtDiffType;

enum
{
    DT_DIFF_TREE_MODEL_COL_NAME,
    DT_DIFF_TREE_MODEL_COL_FILE_TYPE,
    DT_DIFF_TREE_MODEL_COL_DIFFERENT,

    /**
     * An GPtrArray with the DtTreeSourceNode pointers for each source
     */
    DT_DIFF_TREE_MODEL_COL_NODE_ARRAY,
    DT_DIFF_TREE_MODEL_NUM_COLUMNS
};

#define DT_TYPE_DIFF_TREE_MODEL dt_diff_tree_model_get_type()
G_DECLARE_FINAL_TYPE(DtDiffTreeModel, dt_diff_tree_model, DT, DIFF_TREE_MODEL, GtkTreeStore);

DtDiffTreeModel *dt_diff_tree_model_new(gint num_sources, DtTreeSource **sources);

gint dt_diff_tree_model_get_num_sources(DtDiffTreeModel *self);
DtTreeSource *dt_diff_tree_model_get_source(DtDiffTreeModel *self, gint source_index);

/**
 * Translates an iterator in the DtDiffTreeModel to one of the source models.
 */
DtTreeSourceNode *dt_diff_tree_model_get_source_node(DtDiffTreeModel *self,
        gint source_index, GtkTreeIter *iter);

void dt_diff_tree_model_check_difference_async(DtDiffTreeModel *self,
        GtkTreeIter *iter, gint io_priority, GCancellable *cancellable,
        GAsyncReadyCallback callback, gpointer userdata);
gboolean dt_diff_tree_model_check_difference_finish(DtDiffTreeModel *self, GAsyncResult *res, GError **error);

G_END_DECLS

#endif // DIFF_TREE_MODEL_H
