#ifndef SOURCE_HELPERS_H
#define SOURCE_HELPERS_H

#include <gtk/gtk.h>
#include "tree-source.h"
#include "ref-count-struct.h"

G_BEGIN_DECLS

/**
 * An opaque struct that I can use to identify a particular file in a
 * DtDiffTreeModel.
 *
 * These structs are immutable and reference counted, so passing them around
 * should be efficient.
 *
 * They're also set up as a boxed type. See \p DT_TYPE_FILE_KEY.
 */
typedef struct _DtFileKey DtFileKey;

#define DT_TYPE_FILE_KEY dt_file_key_get_type()
UTIL_DECLARE_BOXED_REFCOUNT_FUNCS(DtFileKey, dt_file_key);

/**
 * Creates a DtTreeSource for a command-line argument.
 *
 * This will handle a path to a directory, an archive, or a path within an
 * archive.
 */
DtTreeSource *get_tree_source_for_arg(const char *arg, gboolean follow_symlinks, GError **error);

/**
 * A GCompareDataFunc that calls a GCompareFunc function.
 *
 * This is just a simple adapter to let me use a GCompareFunc for a function
 * that expects a GCompareDataFunc.
 *
 * \param a The left-hand value
 * \param b The right-hand value
 * \param userdata A GCompareFunc pointer to do the actual comparison.
 */
gint compare_3to2(gconstpointer a, gconstpointer b, gpointer userdata);

/**
 * Returns the depth of a row in a tree model. A top-level node is depth 1.
 */
gint get_tree_iter_depth(GtkTreeModel *model, GtkTreeIter *iter);

/**
 * Creates a DtFileKey for a row in a model.
 *
 * \param model A GtkTreeModel with the same column types as either
 * DtSourceTreeModel or DtDiffTreeModel.
 * \param iter The row to look up.
 * \return A new DtFileKey. Free with dt_file_key_unref.
 */
DtFileKey *dt_file_key_from_model(GtkTreeModel *model, GtkTreeIter *iter);

/**
 * Looks for a row in a GtkTreeModel that matches a DtFileKey.
 *
 * \param model The GtkTreeModel to search. This can be either a
 * DtSourceTreeModel or a DtDiffTreeModel, or anything with the same columns.
 * \param[out] iter Returns an iterator.
 * \param key The DtFileKey to look up.
 * \return TRUE if a matching row was found.
 */
gboolean dt_file_key_get_iter(GtkTreeModel *model, GtkTreeIter *iter, DtFileKey *key);

/**
 * Compares two DtFileKey structs.
 */
gint dt_file_key_compare(gconstpointer a, gconstpointer b);

G_END_DECLS

#endif // SOURCE_HELPERS_H
