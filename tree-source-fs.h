#ifndef TREE_SOURCE_FS_H
#define TREE_SOURCE_FS_H

/*
 * TODO:
 * - Should there be a signal in the DtTreeSource interface for some sort of
 * progress callback, or at least to indicate when it's finished? It might not
 * be especially meaningful for some archive formats.
 */

#include <gtk/gtk.h>

#include "tree-source-base.h"

G_BEGIN_DECLS

#define DT_TYPE_TREE_SOURCE_FS dt_tree_source_fs_get_type()
G_DECLARE_FINAL_TYPE(DtTreeSourceFS, dt_tree_source_fs, DT, TREE_SOURCE_FS, DtTreeSourceBase);

DtTreeSourceFS *dt_tree_source_fs_new(GFile *base, gboolean follow_symlinks);

G_END_DECLS

#endif // TREE_SOURCE_FS_H
