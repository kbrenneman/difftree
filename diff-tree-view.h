#ifndef DIFF_TREE_VIEW_H
#define DIFF_TREE_VIEW_H

#include <gtk/gtk.h>

#include "diff-tree-model.h"

G_BEGIN_DECLS

/**
 * A filter function for a GtkTreeModelFilter to hide files that are missing
 * from one or more sources.
 *
 * \p userdata must be a GArray of gboolean values, with one element per
 * source. If an element is TRUE, and the file is missing from that source,
 * then the file is hidden.
 */
gboolean dt_tree_filter_missing_visible(GtkTreeModel *chmodel, GtkTreeIter *iter, gpointer userdata);

/**
 * A sort function for comparing rows in a DtDiffTreeModel.
 */
gint diff_tree_model_row_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer userdata);

/**
 * Creates a GtkTreeView to display a DtDiffTreeModel.
 *
 * This uses the column with the DtTreeSourceNode array, so it will work with
 * a DtTreeModel or a GtkTreeModelFilter/Sort wrapper around it.
 */
GtkTreeView *create_diff_tree_view(gint num_sources, DtTreeSource **sources);

/**
 * Creates a GtkTreeView to display a DtDiffTreeModel.
 *
 * This is just a wrapper around create_diff_tree_view which gets the sources
 * from an existing DtDiffTreeModel.
 *
 * This does *not* set the model to the GtkTreeView, so the caller still has to
 * call gtk_tree_view_set_model.
 */
GtkTreeView *create_diff_tree_view_from_model(DtDiffTreeModel *model);

G_END_DECLS

#endif // DIFF_TREE_VIEW_H
