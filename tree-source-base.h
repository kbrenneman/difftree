#ifndef TREE_SOURCE_BASE_H
#define TREE_SOURCE_BASE_H

/**
 * \file
 *
 * A basic DtTreeSource implementation.
 *
 * This implementation maintains a tree internally, and uses a hashtable to
 * quickly look up child nodes.
 */

#include <glib.h>
#include <glib-object.h>

#include "tree-source.h"

G_BEGIN_DECLS

#define DT_TYPE_TREE_SOURCE_BASE dt_tree_source_base_get_type()
G_DECLARE_DERIVABLE_TYPE(DtTreeSourceBase, dt_tree_source_base, DT, TREE_SOURCE_BASE, GObject);

struct _DtTreeSourceBaseClass
{
    GObjectClass parent_class;
};

void dt_tree_source_base_add_children(DtTreeSourceBase *self, DtTreeSourceNode *parent,
        gint num, GFileInfo **info, DtTreeSourceNode **ret_nodes);

void dt_tree_source_base_remove_children(DtTreeSourceBase *self, DtTreeSourceNode *parent,
        gint num, DtTreeSourceNode **nodes);

void dt_tree_source_base_set_file_info(DtTreeSourceBase *self, DtTreeSourceNode *node, GFileInfo *info);

G_END_DECLS

#endif // TREE_SOURCE_BASE_H
