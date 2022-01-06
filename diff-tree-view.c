#include "diff-tree-view.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>

static const GdkRGBA DIFF_COLOR = { 1.0, 0.5, 0.5, 1.0 };
static const GdkRGBA MISSING_COLOR = { 0.5, 0.5, 1.0, 1.0 };

static void set_cell_background(GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter)
{
    GPtrArray *nodes = NULL;
    DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;
    gboolean missing = FALSE;
    gint i;

    gtk_tree_model_get(model, iter,
            DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff,
            DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodes, -1);

    if (nodes != NULL)
    {
        if (diff != DT_DIFF_TYPE_IDENTICAL)
        {
            for (i=0; i<nodes->len; i++)
            {
                if (nodes->pdata[i] == NULL)
                {
                    missing = TRUE;
                    break;
                }
            }
        }
        g_ptr_array_unref(nodes);
    }

    if (missing)
    {
        g_object_set(cell,
                "cell-background-rgba", &MISSING_COLOR,
                "cell-background-set", TRUE, NULL);
    }
    else if (diff == DT_DIFF_TYPE_DIFFERENT)
    {
        g_object_set(cell,
                "cell-background-rgba", &DIFF_COLOR,
                "cell-background-set", TRUE, NULL);
    }
    else
    {
        g_object_set(cell, "cell-background-set", FALSE, NULL);
    }
}

static void col_data_name(GtkTreeViewColumn *col, GtkCellRenderer *cell,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer userdata)
{
    char *text = NULL;
    gtk_tree_model_get(model, iter,
            DT_DIFF_TREE_MODEL_COL_NAME, &text, -1);
    g_object_set(cell, "text", text, NULL);
    set_cell_background(cell, model, iter);
}

static void col_data_icon(GtkTreeViewColumn *col, GtkCellRenderer *cell,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer userdata)
{
    // See here for the standard icon names:
    // https://developer.gnome.org/icon-naming-spec/
    GtkTreeView *view = GTK_TREE_VIEW(userdata);
    GFileType type = G_FILE_TYPE_UNKNOWN;
    const char *icon = "emblem-unreadable";

    gtk_tree_model_get(model, iter,
            DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type, -1);
    if (type == G_FILE_TYPE_REGULAR)
    {
        icon = "text-x-generic";
    }
    else if (type == G_FILE_TYPE_DIRECTORY)
    {
        GtkTreePath *path = gtk_tree_model_get_path(model, iter);
        icon = "folder";
        if (path != NULL)
        {
            if (view != NULL && gtk_tree_view_row_expanded(view, path))
            {
                icon = "folder-open";
            }
            gtk_tree_path_free(path);
        }
    }
    else if (type == G_FILE_TYPE_SYMBOLIC_LINK)
    {
        icon = "emblem-symbolic-link";
    }
    g_object_set(cell, "icon-name", icon, NULL);
    set_cell_background(cell, model, iter);
}

static void col_data_diff(GtkTreeViewColumn *col, GtkCellRenderer *cell,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer userdata)
{
    const char *text = "";
    DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;

    gtk_tree_model_get(model, iter, DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff, -1);
    switch (diff)
    {
        case DT_DIFF_TYPE_UNKNOWN: text = ""; break;
        case DT_DIFF_TYPE_IDENTICAL: text = "SAME"; break;
        case DT_DIFF_TYPE_DIFFERENT: text = "DIFF"; break;
        default: text = ""; break;
    }
    g_object_set(cell, "text", text, NULL);
    set_cell_background(cell, model, iter);
}

static gboolean on_tree_key_press(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    // Use the left/right cursor keys to navigate the tree
    GdkEventKey *kevt = &event->key;
    GdkModifierType mask = kevt->state & (GDK_CONTROL_MASK | GDK_META_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK);
    //GtkTreeView *view = GTK_TREE_VIEW(user_data);
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    gboolean ret = FALSE;

    //print "_onTreeKeyPress: %r, %r, %r, 0x%x -> 0x%x" % (evt, type(evt), evt.keyval, evt.state, mask)
    if (mask == 0 && (kevt->keyval == GDK_KEY_Left || kevt->keyval == GDK_KEY_Right))
    {
        GtkTreePath *cursor = NULL;
        gtk_tree_view_get_cursor(view, &cursor, NULL);
        if (cursor != NULL)
        {
            if (kevt->keyval == GDK_KEY_Left)
            {
                if (gtk_tree_view_row_expanded(view, cursor))
                {
                    gtk_tree_view_collapse_row(view, cursor);
                    ret = TRUE;
                }
                else
                {
                    if (gtk_tree_path_up(cursor))
                    {
                        gtk_tree_view_set_cursor(view, cursor, NULL, FALSE);
                        ret = TRUE;
                    }
                }
            }
            else if (kevt->keyval == GDK_KEY_Right)
            {
                if (gtk_tree_view_row_expanded(view, cursor))
                {
                    GtkTreeIter iter;
                    if (gtk_tree_model_get_iter(model, &iter, cursor))
                    {
                        if (gtk_tree_model_iter_has_child(model, &iter))
                        {
                            gtk_tree_path_down(cursor);
                            gtk_tree_view_set_cursor(view, cursor, NULL, FALSE);
                            ret = TRUE;
                        }
                    }
                }
                else
                {
                    gtk_tree_view_expand_row(view, cursor, FALSE);
                    ret = TRUE;
                }
            }
            gtk_tree_path_free(cursor);
        }
    }
    return ret;
}

typedef struct
{
    gint index;
    DtTreeSource *source;
} FileInfoColParam;

static FileInfoColParam *file_info_col_param_alloc(gint index, DtTreeSource *source)
{
    FileInfoColParam *param = g_malloc(sizeof(FileInfoColParam));
    param->index = index;
    param->source = g_object_ref(source);
    return param;
}

static void file_info_col_param_free(gpointer ptr)
{
    if (ptr != NULL)
    {
        FileInfoColParam *param = ptr;
        if (param->source != NULL)
        {
            g_object_unref(param->source);
        }
        g_free(param);
    }
}

static void col_data_size(GtkTreeViewColumn *col, GtkCellRenderer *cell,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer userdata)
{
    const FileInfoColParam *param = userdata;
    GFileInfo *info = NULL;
    GPtrArray *nodes = NULL;

    gtk_tree_model_get(model, iter, DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodes, -1);
    if (nodes != NULL)
    {
        if (nodes->pdata[param->index] != NULL)
        {
            info = dt_tree_source_get_file_info(param->source, nodes->pdata[param->index]);
        }
        g_ptr_array_unref(nodes);
    }

    if (info != NULL && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SIZE))
    {
        gchar buf[32];
        g_snprintf(buf, sizeof(buf), "%'lld", (unsigned long long) g_file_info_get_size(info));
        g_object_set(cell, "text", buf, NULL);
    }
    else
    {
        g_object_set(cell, "text", "", NULL);
    }
    set_cell_background(cell, model, iter);
}

static void col_data_time(GtkTreeViewColumn *col, GtkCellRenderer *cell,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer userdata)
{

    const FileInfoColParam *param = userdata;
    GFileInfo *info = NULL;
    GPtrArray *nodes = NULL;

    gtk_tree_model_get(model, iter, DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodes, -1);
    if (nodes != NULL)
    {
        if (nodes->pdata[param->index] != NULL)
        {
            info = dt_tree_source_get_file_info(param->source, nodes->pdata[param->index]);
        }
        g_ptr_array_unref(nodes);
    }

    if (info != NULL && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED))
    {
        gchar *buf;
        GTimeVal tv = {};
        GDateTime *tm;

        g_file_info_get_modification_time(info, &tv);
        tm = g_date_time_new_from_unix_local(tv.tv_sec);

        buf = g_date_time_format(tm, "%y-%d-%m %H:%M:%S");
        g_object_set(cell, "text", buf, NULL);
        g_free(buf);
        g_date_time_unref(tm);
    }
    else
    {
        g_object_set(cell, "text", "", NULL);
    }
    set_cell_background(cell, model, iter);
}

GtkTreeView *create_diff_tree_view(gint num_sources, DtTreeSource **sources)
{
    GtkTreeView *view = GTK_TREE_VIEW(gtk_tree_view_new());
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    gint i;

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Name");
    gtk_tree_view_append_column(view, col);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_cell_data_func(col, renderer, col_data_icon, view, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(col, renderer, col_data_name, NULL, NULL);

    gtk_tree_view_insert_column_with_data_func(view, -1, "Diff",
            gtk_cell_renderer_text_new(),
            col_data_diff, NULL, NULL);

    for (i=0; i<num_sources; i++)
    {
        gchar buf[64];
        g_snprintf(buf, sizeof(buf), "Size %d", i);
        gtk_tree_view_insert_column_with_data_func(view, -1, buf,
                gtk_cell_renderer_text_new(), col_data_size,
                file_info_col_param_alloc(i, sources[i]), file_info_col_param_free);

        g_snprintf(buf, sizeof(buf), "Time %d", i);
        gtk_tree_view_insert_column_with_data_func(view, -1, buf,
                gtk_cell_renderer_text_new(), col_data_time,
                file_info_col_param_alloc(i, sources[i]), file_info_col_param_free);
    }

    g_signal_connect(view, "key-press-event", G_CALLBACK(on_tree_key_press), NULL);

    gtk_tree_view_set_search_column(view, DT_DIFF_TREE_MODEL_COL_NAME);

    return view;
}

GtkTreeView *create_diff_tree_view_from_model(DtDiffTreeModel *model)
{
    gint num_sources = dt_diff_tree_model_get_num_sources(model);
    DtTreeSource **sources = g_malloc(num_sources * sizeof(DtTreeSource *));
    GtkTreeView *view;
    gint i;

    for (i=0; i<num_sources; i++)
    {
        sources[i] = dt_diff_tree_model_get_source(model, i);
    }
    view = create_diff_tree_view(num_sources, sources);
    g_free(sources);
    return view;
}

gboolean dt_tree_filter_missing_visible(GtkTreeModel *chmodel, GtkTreeIter *iter, gpointer userdata)
{
    GPtrArray *nodes = NULL;
    GArray *hide_missing = userdata;
    gboolean result = TRUE;

    gtk_tree_model_get(chmodel, iter, DT_DIFF_TREE_MODEL_COL_NODE_ARRAY, &nodes, -1);
    if (nodes != NULL)
    {
        gint i;
        g_assert(nodes->len == hide_missing->len);
        for (i=0; i<nodes->len; i++)
        {
            if (g_array_index(hide_missing, gboolean, i))
            {
                if (nodes->pdata[i] == NULL)
                {
                    result = FALSE;
                    break;
                }
            }
        }
        g_ptr_array_unref(nodes);
    }
    return result;
}

static gint get_sort_group(GFileType type)
{
    switch (type)
    {
        case G_FILE_TYPE_DIRECTORY: return 0;
        default: return 1;
    }
}

gint diff_tree_model_row_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer userdata)
{
    GFileType type1 = G_FILE_TYPE_UNKNOWN;
    GFileType type2 = G_FILE_TYPE_UNKNOWN;
    gint diff = 0;

    gtk_tree_model_get(model, a, DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type1, -1);
    gtk_tree_model_get(model, b, DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type2, -1);
    if (type1 != type2)
    {
        gint group1 = get_sort_group(type1);
        gint group2 = get_sort_group(type2);
        diff = group1 - group2;
    }

    if (diff == 0)
    {
        gchar *name1 = NULL;
        gchar *name2 = NULL;
        gtk_tree_model_get(model, a, DT_DIFF_TREE_MODEL_COL_NAME, &name1, -1);
        gtk_tree_model_get(model, b, DT_DIFF_TREE_MODEL_COL_NAME, &name2, -1);
        if (name1 == NULL && name2 == NULL)
        {
            diff = 0;
        }
        else if (name1 == NULL)
        {
            diff = -1;
        }
        else if (name2 == NULL)
        {
            diff = 1;
        }
        else
        {
            diff = strcasecmp(name1, name2);
            if (diff == 0)
            {
                diff = strcmp(name1, name2);
            }
        }
        g_free(name1);
        g_free(name2);
    }

    if (diff == 0)
    {
        diff = type1 - type2;
    }

    return diff;
}

