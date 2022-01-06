#ifndef TREE_SOURCE_ZIP_H
#define TREE_SOURCE_ZIP_H

#include <gtk/gtk.h>

#include "tree-source-base.h"
#include "zipfd.h"

G_BEGIN_DECLS

#define DT_TYPE_TREE_SOURCE_ZIP dt_tree_source_zip_get_type()
G_DECLARE_FINAL_TYPE(DtTreeSourceZip, dt_tree_source_zip, DT, TREE_SOURCE_ZIP, DtTreeSourceBase);

DtTreeSourceZip *dt_tree_source_zip_new(DtZipFile *zipsource, const char *subdir, GError **error);
DtTreeSourceZip *dt_tree_source_zip_new_for_path(const char *path, const char *subdir, GError **error);

G_END_DECLS

#endif // TREE_SOURCE_ZIP_H

