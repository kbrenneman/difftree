#include "source-helpers.h"

#include "diff-tree-model.h"
#include "tree-source.h"
#include "tree-source-fs.h"
#include "tree-source-zip.h"

struct _DtFileKey
{
    UtilRefCountedBase refcount;

    /**
     * The GFileType for the row. All parent rows should be
     * G_FILE_TYPE_DIRECTORY.
     */
    GFileType type;
    gchar **names;
    gint depth;
};

static void dt_file_key_free(DtFileKey *key);

UTIL_DEFINE_BOXED_REFCOUNT_TYPE(DtFileKey, dt_file_key, dt_file_key_free)

/**
 * Looks for a regular file that's an ancestor of \p gf.
 *
 * This is used to deal with a path within an archive.
 */
static GFile *find_parent_file(GFile *gf, GError **error)
{
    GFile *parent = g_object_ref(gf);

    while (parent != NULL)
    {
        GFileInfo *info = NULL;

        info = g_file_query_info(parent, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (info != NULL)
        {
            if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
            {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                        "%s is not a regular file", g_file_peek_path(parent));
                g_clear_object(&parent);
            }
            g_object_unref(info);
            return parent;
        }

        g_set_object(&parent, g_file_get_parent(parent));
    }

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
            "Can't find file");
    return NULL;
}

DtTreeSource *get_tree_source_for_arg(const char *arg, gboolean follow_symlinks, GError **error)
{
    GFile *gf = g_file_new_for_path(arg);
    GFile *parent = NULL;
    GFileInfo *info = NULL;

    info = g_file_query_info(gf, G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info != NULL)
    {
        DtTreeSource *source = NULL;
        if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY)
        {
            source = DT_TREE_SOURCE(dt_tree_source_fs_new(gf, follow_symlinks));
        }
        else if (g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR)
        {
            source = DT_TREE_SOURCE(dt_tree_source_zip_new_for_path(arg, NULL, error));
        }
        else
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Invalid file type %d", g_file_info_get_file_type(info));
        }
        g_object_unref(info);
        g_object_unref(gf);
        return source;
    }

    // The target doesn't exist. See if any of the parent paths exist, to see
    // if this is a path inside an archive.
    parent = find_parent_file(gf, error);
    if (parent != NULL)
    {
        const char *archivePath = g_file_peek_path(parent);
        char *relPath = g_file_get_relative_path(parent, gf);
        DtTreeSource *source = NULL;

        archivePath = g_file_peek_path(parent);
        if (archivePath != NULL && relPath != NULL)
        {
            source = DT_TREE_SOURCE(dt_tree_source_zip_new_for_path(archivePath, relPath, error));
        }
        else
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Internal error: Can't get archive path");
        }
        g_free(relPath);
        g_object_unref(gf);
        g_object_unref(parent);
        return source;
    }
    else
    {
        g_object_unref(gf);
        return NULL;
    }
}

gint compare_3to2(gconstpointer a, gconstpointer b, gpointer userdata)
{
    GCompareFunc func = userdata;
    return func(a, b);
}

gint get_tree_iter_depth(GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkTreeIter parent = *iter;
    gint depth = 0;

    while (TRUE)
    {
        GtkTreeIter next;
        depth++;
        if (!gtk_tree_model_iter_parent(model, &next, &parent))
        {
            break;
        }
        parent = next;
    }
    return depth;
}

DtFileKey *dt_file_key_from_model(GtkTreeModel *model, GtkTreeIter *iter)
{
    DtFileKey *key;
    GtkTreeIter parent = *iter;
    gint depth = get_tree_iter_depth(model, iter);
    gint i;

    key = g_malloc(sizeof(DtFileKey) +
            (depth + 1) * sizeof(gchar *));
    util_ref_counted_struct_init(&key->refcount);
    key->type = G_FILE_TYPE_UNKNOWN;
    key->names = (gchar **) (key + 1);
    key->depth = depth;
    //key->refcount = 1;

    for (i=depth - 1; i >= 0; i--)
    {
        GtkTreeIter next;
        GFileType type;
        gtk_tree_model_get(model, &parent,
                DT_DIFF_TREE_MODEL_COL_NAME, &key->names[i],
                DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type, -1);
        if (i == depth - 1)
        {
            key->type = type;
        }
        else
        {
            g_assert(type == G_FILE_TYPE_DIRECTORY);
        }

        if (!gtk_tree_model_iter_parent(model, &next, &parent))
        {
            g_assert(i == 0);
        }
        parent = next;
    }
    g_assert(key->type != G_FILE_TYPE_UNKNOWN);
    key->names[depth] = NULL;

    return key;
}

static void dt_file_key_free(DtFileKey *key)
{
    gint i;
    for (i=0; i<key->depth; i++)
    {
        g_free(key->names[i]);
    }
    g_free(key);
}

gint dt_file_key_compare(gconstpointer a, gconstpointer b)
{
    const DtFileKey *key1 = a;
    const DtFileKey *key2 = b;

    if (key1 == NULL && key2 == NULL)
    {
        return 0;
    }
    else if (key1 == NULL)
    {
        return -1;
    }
    else if (key2 == NULL)
    {
        return 1;
    }
    else if (key1->depth != key2->depth)
    {
        return key1->depth - key2->depth;
    }
    else if (key1->type != key2->type)
    {
        return key1->type - key2->type;
    }
    else
    {
        gint i;
        for (i=0; i<key1->depth; i++)
        {
            gint diff = strcmp(key1->names[i], key2->names[i]);
            if (diff != 0)
            {
                return diff;
            }
        }
    }

    return 0;
}

static gboolean dt_file_key_find_child(GtkTreeModel *model, GtkTreeIter *iter,
        GtkTreeIter *parent, GFileType type, const char *name)
{
    GtkTreeIter next;
    gboolean ok;

    for (ok = gtk_tree_model_iter_children(model, &next, parent);
            ok; ok = gtk_tree_model_iter_next(model, &next))
    {
        gboolean matches = FALSE;
        GFileType rowType = G_FILE_TYPE_UNKNOWN;

        gtk_tree_model_get(model, &next,
                DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &rowType, -1);
        if (type == rowType)
        {
            gchar *rowName = NULL;
            gtk_tree_model_get(model, &next,
                    DT_DIFF_TREE_MODEL_COL_NAME, &rowName, -1);
            if (strcmp(rowName, name) == 0)
            {
                matches = TRUE;
            }
            g_free(rowName);
        }
        if (matches)
        {
            *iter = next;
            return TRUE;
        }
    }
    return FALSE;
}

gboolean dt_file_key_get_iter(GtkTreeModel *model, GtkTreeIter *iter, DtFileKey *key)
{
    gint i;

    g_assert(key->depth > 0);

    for (i=0; i<key->depth; i++)
    {
        GtkTreeIter *parent = (i > 0) ? iter : NULL;
        GFileType type = (i < key->depth - 1) ? G_FILE_TYPE_DIRECTORY : key->type;
        if (!dt_file_key_find_child(model, iter, parent, type, key->names[i]))
        {
            return FALSE;
        }
    }
    return TRUE;
}
