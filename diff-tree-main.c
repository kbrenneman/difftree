#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib.h>

#include "diff-tree-model.h"
#include "diff-tree-view.h"
#include "source-helpers.h"
#include "app-config.h"
#include "settings-window.h"

typedef struct
{
    DiffTreeConfig *config;
    GtkWindow *window;
    DtDiffTreeModel *diff_model;
    GtkTreeModelFilter *missing_filter;
    GtkTreeView *view;
    GtkCheckMenuItem **hide_missing_menus;
    GArray *hide_missing_flags;

    /**
     * True if the DtDiffTreeModel needs to be updated to refilter missing
     * files.
     */
    gboolean needs_missing_filter_update;

    GQueue *diff_check_queue;
    gboolean diff_check_running;
    gint num_scans_running;
} WindowData;

static const char *get_gerror_message(GError *error)
{
    if (error != NULL && error->message != NULL)
    {
        return error->message;
    }
    else
    {
        return "(No error message)";
    }
}

static void show_error_message(GtkWindow *parent, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
void show_error_message(GtkWindow *parent, const gchar *format, ...)
{
    va_list args;
    gchar *text;
    GtkDialog *dlg;

    va_start(args, format);
    text = g_strdup_vprintf(format, args);
    va_end(args);

    dlg = GTK_DIALOG(gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", text));
    gtk_dialog_run(dlg);
    gtk_widget_destroy(GTK_WIDGET(dlg));
    g_free(text);
}

static GPtrArray *create_sources(const char * const *paths, int num_sources,
        gboolean follow_symlinks, GError **error)
{
    GPtrArray *sources;
    gboolean success = TRUE;
    gint i;

    sources = g_ptr_array_new_full(num_sources, g_object_unref);

    for (i=0; i<num_sources; i++)
    {
        DtTreeSource *source = get_tree_source_for_arg(paths[i], follow_symlinks, error);
        if (source == NULL)
        {
            g_prefix_error(error, "Can't open %s: ", paths[i]);
            success = FALSE;
            break;
        }
        g_ptr_array_insert(sources, -1, source);
    }

    if (!success)
    {
        g_ptr_array_unref(sources);
        return NULL;
    }

    return sources;
}

static gboolean show_file(const char *diff_command, DtDiffTreeModel *model, GtkTreeIter *iter, GError **error)
{
    gint *sources = g_malloc(dt_diff_tree_model_get_num_sources(model) * sizeof(gint));
    gint numFiles = 0;
    gboolean ret = FALSE;
    gint i;

    for (i=0; i<dt_diff_tree_model_get_num_sources(model); i++)
    {
        DtTreeSourceNode *node = dt_diff_tree_model_get_source_node(model, i, iter);
        if (node != NULL)
        {
            sources[numFiles++] = i;
        }
    }

    if (numFiles >= 2)
    {
        GFile **files = g_malloc0(numFiles * sizeof(GFile *));
        int argc = 0;
        char **argv = NULL;
        GPid child;

        if (!g_shell_parse_argv(diff_command, &argc, &argv, error))
        {
            goto done;
        }
        if (argc < 1)
        {
            g_warning("No diff command set.\n");
            g_free(argv);
            ret = TRUE;
            goto done;
        }

        for (i=0; i<numFiles; i++)
        {
            gint index = sources[i];
            files[i] = dt_diff_tree_model_get_fs_file(model, iter, index, error);
            if (files[i] == NULL)
            {
                g_strfreev(argv);
                g_free(files);
                goto done;
            }
        }

        // Allocate enough space to append the filenames
        argv = g_realloc(argv, (argc + numFiles + 1) * sizeof(gchar *));
        for (i=0; i<numFiles; i++)
        {
            argv[argc++] = g_file_get_path(files[i]);
        }
        argv[argc] = NULL;

        if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &child, error))
        {
            g_debug("Started child %" G_PID_FORMAT, child);
            ret = TRUE;
        }
        g_free(files);
        g_strfreev(argv);
    }
    else if (numFiles == 1)
    {
        GFile *gf = dt_diff_tree_model_get_fs_file(model, iter, sources[0], error);
        if (gf != NULL)
        {
            char *uri = g_file_get_uri(gf);
            if (uri != NULL)
            {
                g_debug("Starting viewer for %s", uri);
                ret = g_app_info_launch_default_for_uri(uri, NULL, error);
                g_debug("g_app_info_launch_default_for_uri returned %d", (int) ret);
                g_free(uri);
            }
            else
            {
                g_warning("Can't get URI for file %s", g_file_peek_path(gf));
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't get URI for file?");
            }
        }
    }

done:
    g_free(sources);
    return ret;
}

static void on_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer userdata)
{
    WindowData *win = userdata;
    GtkTreeIter viewIter, iter;
    GFileType type = G_FILE_TYPE_UNKNOWN;
    GError *error = NULL;

    g_assert(gtk_tree_view_get_model(view) == GTK_TREE_MODEL(win->missing_filter));

    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(win->missing_filter), &viewIter, path))
    {
        g_warning("Can't get iter for path");
        return;
    }

    gtk_tree_model_filter_convert_iter_to_child_iter(win->missing_filter, &iter, &viewIter);

    // TODO: Change DtDiffProcessManager to take an array of DtTreeSource
    // objects. Then, I can pass a DtTreeSourceNode from the
    // GtkTreeModelFilter, without needing to translate the GtkTreeIter.
    gtk_tree_model_get(GTK_TREE_MODEL(win->diff_model), &iter,
            DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type, -1);
    if (type == G_FILE_TYPE_DIRECTORY)
    {
        if (gtk_tree_view_row_expanded(view, path))
        {
            gtk_tree_view_collapse_row(view, path);
        }
        else
        {
            gtk_tree_view_expand_row(view, path, FALSE);
        }
    }
    else if (type == G_FILE_TYPE_REGULAR || type == G_FILE_TYPE_SYMBOLIC_LINK)
    {
        if (!show_file(win->config->diff_command_line, win->diff_model, &iter, &error))
        {
            if (error != NULL)
            {
                show_error_message(win->window, "Failed to start diff tool: %s\n", get_gerror_message(error));
                g_clear_error(&error);
            }
        }
    }
}

static gboolean get_next_diff_check(WindowData *win, GtkTreeIter *iter)
{
    while (!g_queue_is_empty(win->diff_check_queue))
    {
        DtFileKey *key = g_queue_pop_head(win->diff_check_queue);
        DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;

        if (!dt_file_key_get_iter(GTK_TREE_MODEL(win->diff_model), iter, key))
        {
            dt_file_key_unref(key);
            continue;
        }
        dt_file_key_unref(key);

        gtk_tree_model_get(GTK_TREE_MODEL(win->diff_model), iter,
                DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff, -1);
        if (diff != DT_DIFF_TYPE_UNKNOWN)
        {
            continue;
        }

        return TRUE;
    }
    return FALSE;
}

static void on_diff_check_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata);

static void start_next_diff_check(WindowData *win)
{
    if (!win->diff_check_running)
    {
        GtkTreeIter iter;
        if (get_next_diff_check(win, &iter))
        {
            win->diff_check_running = TRUE;
            dt_diff_tree_model_check_difference_async(win->diff_model,
                    &iter, G_PRIORITY_DEFAULT, NULL,
                    on_diff_check_ready, win);
        }
    }
}

static void on_diff_check_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    WindowData *win = userdata;
    GError *error = NULL;

    win->diff_check_running = FALSE;

    if (!dt_diff_tree_model_check_difference_finish(win->diff_model, res, &error))
    {
        show_error_message(win->window, "%s", get_gerror_message(error));
        g_clear_error(&error);
    }

    start_next_diff_check(win);
}

static void add_diff_check(WindowData *win, GtkTreeIter *iter)
{
    GFileType type = DT_DIFF_TYPE_UNKNOWN;
    DtDiffType diff = DT_DIFF_TYPE_UNKNOWN;
    DtFileKey *key = NULL;

    gtk_tree_model_get(GTK_TREE_MODEL(win->diff_model), iter,
            DT_DIFF_TREE_MODEL_COL_FILE_TYPE, &type,
            DT_DIFF_TREE_MODEL_COL_DIFFERENT, &diff, -1);
    if (type != G_FILE_TYPE_REGULAR || diff != DT_DIFF_TYPE_UNKNOWN)
    {
        return;
    }

    // TODO: Use a GTree to keep track of which rows are in progress?
    key = dt_file_key_from_model(GTK_TREE_MODEL(win->diff_model), iter);
    if (g_queue_find_custom(win->diff_check_queue, key, dt_file_key_compare) != NULL)
    {
        dt_file_key_unref(key);
        return;
    }

    g_queue_push_tail(win->diff_check_queue, key);

    start_next_diff_check(win);
}

static void on_menu_item_check_files(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(win->view);
    GList *paths = gtk_tree_selection_get_selected_rows(sel, NULL);
    GList *node;

    for (node = paths; node != NULL; node = node->next)
    {
        GtkTreePath *path = node->data;
        GtkTreeIter viewIter, iter;

        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(win->missing_filter), &viewIter, path))
        {
            //GError *error = NULL;
            gtk_tree_model_filter_convert_iter_to_child_iter(win->missing_filter, &iter, &viewIter);

            add_diff_check(win, &iter);
        }
    }
    g_list_free_full(paths, (GDestroyNotify) gtk_tree_path_free);
}

static void on_menu_item_settings(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;
    dt_settings_editor_show_dialog(win->window, win->config);
}

static void on_menu_item_quit(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;
    gtk_widget_destroy(GTK_WIDGET(win->window));
}

static gboolean update_missing_filter(gpointer userdata)
{
    WindowData *win = userdata;
    gboolean changed = FALSE;
    gint i;

    if (!win->needs_missing_filter_update)
    {
        g_debug("Missing files filter is up to date\n");
        return G_SOURCE_REMOVE;
    }
    win->needs_missing_filter_update = FALSE;

    for (i=0; i<win->hide_missing_flags->len; i++)
    {
        gboolean hide = gtk_check_menu_item_get_active(win->hide_missing_menus[i]);
        if (g_array_index(win->hide_missing_flags, gboolean, i) != hide)
        {
            g_array_index(win->hide_missing_flags, gboolean, i) = hide;
            changed = TRUE;
        }
    }

    if (changed)
    {
        g_debug("Updating missing files filter\n");
        gtk_tree_model_filter_refilter(win->missing_filter);
    }

    return G_SOURCE_REMOVE;
}

static void on_menu_item_hide_all(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;
    gint num_sources = dt_diff_tree_model_get_num_sources(win->diff_model);
    gint i;

    for (i=0; i<num_sources; i++)
    {
        gtk_check_menu_item_set_active(win->hide_missing_menus[i], TRUE);
    }
}

static void on_menu_item_show_all(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;
    gint num_sources = dt_diff_tree_model_get_num_sources(win->diff_model);
    gint i;

    for (i=0; i<num_sources; i++)
    {
        gtk_check_menu_item_set_active(win->hide_missing_menus[i], FALSE);
    }
}

static void on_menu_item_toggle_missing(GtkMenuItem *item, gpointer userdata)
{
    WindowData *win = userdata;

    // The checkbox state is already toggled, so just queue an update for the
    // tree.
    if (!win->needs_missing_filter_update)
    {
        win->needs_missing_filter_update = TRUE;
        g_idle_add(update_missing_filter, win);
    }
}

static GtkWidget *add_menu_item(WindowData *win, GtkMenuShell *parent, const char *name,
        GtkAccelGroup *accel_group, gint accel_key, GdkModifierType accel_mods,
        void (* activate_callback) (GtkMenuItem *item, gpointer userdata))
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(name);
    if (accel_group != NULL)
    {
        gtk_widget_add_accelerator(item, "activate", accel_group, accel_key, accel_mods, GTK_ACCEL_VISIBLE);
    }
    if (activate_callback != NULL)
    {
        g_signal_connect(item, "activate", G_CALLBACK(activate_callback), win);
    }
    if (parent != NULL)
    {
        gtk_menu_shell_append(parent, item);
    }
    return item;
}

static GtkMenuBar *create_menu(WindowData *win, GtkAccelGroup *accel_group)
{
    GtkMenuBar *top = GTK_MENU_BAR(gtk_menu_bar_new());
    GtkMenuShell *menu;
    GtkWidget *item;
    gint i;

    menu = GTK_MENU_SHELL(gtk_menu_new());
    item = gtk_menu_item_new_with_mnemonic("_File");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(menu));
    gtk_menu_shell_append(GTK_MENU_SHELL(top), item);
    add_menu_item(win, menu, "_Check Files", accel_group,
            GDK_KEY_d, GDK_CONTROL_MASK, on_menu_item_check_files);
    add_menu_item(win, menu, "_Settings", NULL, 0, 0, on_menu_item_settings);
    add_menu_item(win, menu, "_Quit", accel_group,
            GDK_KEY_q, GDK_CONTROL_MASK, on_menu_item_quit);

    menu = GTK_MENU_SHELL(gtk_menu_new());
    item = gtk_menu_item_new_with_mnemonic("_Missing Files");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(menu));
    gtk_menu_shell_append(GTK_MENU_SHELL(top), item);

    add_menu_item(win, menu, "_Show All", NULL, 0, 0, on_menu_item_show_all);
    add_menu_item(win, menu, "_Hide All", NULL, 0, 0, on_menu_item_hide_all);
    win->hide_missing_menus = g_malloc(dt_diff_tree_model_get_num_sources(win->diff_model) * sizeof(GtkCheckMenuItem *));
    for (i=0; i<dt_diff_tree_model_get_num_sources(win->diff_model); i++)
    {
        gchar label[64];

        if (i <= 9)
        {
            snprintf(label, sizeof(label), "Hide Missing From _%d", i);
            item = gtk_check_menu_item_new_with_mnemonic(label);
        }
        else
        {
            snprintf(label, sizeof(label), "Hide Missing From %d", i);
            item = gtk_check_menu_item_new_with_label(label);
        }
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                g_array_index(win->hide_missing_flags, gboolean, i));
        g_signal_connect(item, "toggled", G_CALLBACK(on_menu_item_toggle_missing), win);
        win->hide_missing_menus[i] = GTK_CHECK_MENU_ITEM(item);
        gtk_menu_shell_append(menu, item);
    }

    return top;
}

static gboolean on_window_configure(GtkWidget *widget, GdkEvent *evt, gpointer userdata)
{
    DiffTreeConfig *config = userdata;

    config->window_width = evt->configure.width;
    config->window_height = evt->configure.height;

    return FALSE;
}

static void on_window_destroy(GtkWidget *widget, gpointer userdata)
{
    gtk_main_quit();
}

static void init_gui(WindowData *win)
{
    GtkAccelGroup *accel_group;
    GtkWidget *swin;
    GtkMenuBar *menu;
    GtkBox *content;

    win->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(win->window, accel_group);

    win->missing_filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(win->diff_model), NULL));
    gtk_tree_model_filter_set_visible_func(win->missing_filter,
            dt_tree_filter_missing_visible, g_array_ref(win->hide_missing_flags),
            (GDestroyNotify) g_array_unref);

    win->view = create_diff_tree_view_from_model(win->diff_model);
    gtk_tree_view_set_model(win->view, GTK_TREE_MODEL(win->missing_filter));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(win->view), GTK_SELECTION_MULTIPLE);

    g_signal_connect(win->view, "row-activated", G_CALLBACK(on_row_activated), win);

    content = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    menu = create_menu(win, accel_group);
    gtk_box_pack_start(content, GTK_WIDGET(menu), FALSE, FALSE, 0);

    swin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(swin), GTK_WIDGET(win->view));
    gtk_box_pack_start(content, swin, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(win->window), GTK_WIDGET(content));

    g_signal_connect(win->window, "configure-event", G_CALLBACK(on_window_configure), win->config);
    gtk_window_set_default_size(GTK_WINDOW(win->window), win->config->window_width, win->config->window_height);

    gtk_widget_show_all(GTK_WIDGET(content));
}

static void on_source_scan_ready(GObject *sourceobj, GAsyncResult *res, gpointer userdata)
{
    WindowData *win = userdata;
    DtTreeSource *source = DT_TREE_SOURCE(sourceobj);
    GError *error = NULL;

    g_print("Scan finished.\n");

    if (!dt_tree_source_scan_finish(source, res, &error))
    {
        show_error_message(win->window, "Error in reading source files: %s\n", get_gerror_message(error));
        g_clear_error(&error);
    }
    g_assert(win->num_scans_running > 0);
    win->num_scans_running--;
    if (win->num_scans_running == 0)
    {
        gtk_window_set_title(win->window, "DiffTree");
    }
}

WindowData *create_main_window(DiffTreeConfig *config, GPtrArray *sources)
{
    WindowData *win = g_malloc0(sizeof(WindowData));
    gint i;

    win->config = diff_tree_config_ref(config);
    win->diff_model = dt_diff_tree_model_new(sources->len, (DtTreeSource **) sources->pdata, 0, NULL);
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(win->diff_model),
            diff_tree_model_row_compare, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(win->diff_model),
            GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    win->diff_check_queue = g_queue_new();

    win->hide_missing_flags = g_array_sized_new(FALSE, TRUE, sizeof(gboolean), sources->len);
    g_array_set_size(win->hide_missing_flags, dt_diff_tree_model_get_num_sources(win->diff_model));

    init_gui(win);

    // Start reading the sources
    for (i=0; i<dt_diff_tree_model_get_num_sources(win->diff_model); i++)
    {
        DtTreeSource *source = dt_diff_tree_model_get_source(win->diff_model, i);
        win->num_scans_running++;
        dt_tree_source_scan_async(source, G_PRIORITY_DEFAULT, NULL,
                on_source_scan_ready, win);
    }
    g_signal_connect(win->window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    gtk_widget_show(GTK_WIDGET(win->window));

    gtk_window_set_title(win->window, "DiffTree (scanning)");

    return win;
}

void cleanup_main_window(WindowData *win)
{
    if (win != NULL)
    {
        if (win->diff_model != NULL)
        {
            dt_diff_tree_model_cleanup_temp_files(win->diff_model);
        }

        g_queue_free_full(win->diff_check_queue, (GDestroyNotify) dt_file_key_unref);
        g_clear_object(&win->diff_model);
        g_clear_object(&win->missing_filter);
        g_free(win->hide_missing_menus);
        g_array_unref(win->hide_missing_flags);
        diff_tree_config_unref(win->config);

        g_free(win);
    }
}

int main(int argc, char **argv)
{
    char *config_file = NULL;
    char *option_diff_command = NULL;
    gboolean option_follow_symlinks = TRUE;
    char **paths = NULL;
    gint num_sources = 0;

    const GOptionEntry options[] =
    {
        { "config", 0, 0, G_OPTION_ARG_FILENAME, &config_file,
            "Config file", "PATH" },
        { "diff-command", 0, 0, G_OPTION_ARG_STRING, &option_diff_command,
            "Specify an external diff tool for comparing files", "COMMAND_LINE" },
        { "follow-symlinks", 0, 0, G_OPTION_ARG_NONE, &option_follow_symlinks,
            "Dereference symlinks and show targets", NULL },
        { "no-follow-symlinks", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &option_follow_symlinks,
            "Dereference symlinks and show targets", NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &paths,
            "Paths to view", "PATH1 PATH2 [PATH3...]" },
        { NULL }
    };
    WindowData *win = NULL;
    DiffTreeConfig *config = NULL;
    GPtrArray *sources = NULL;
    GError *error = NULL;
    gint ret = 2;

    if (!gtk_init_with_args(&argc, &argv, NULL, options, NULL, &error))
    {
        printf("%s\n", error->message);
        g_clear_error(&error);
        goto done;
    }

    if (paths != NULL)
    {
        while (paths[num_sources] != NULL)
        {
            num_sources++;
        }
    }
    if (num_sources < 2)
    {
        printf("Usage: %s PATH1 PATH2 [PATH3...]\n", argv[0]);
        goto done;
    }

    sources = create_sources((const char * const *)paths, num_sources, option_follow_symlinks, &error);
    if (sources == NULL)
    {
        show_error_message(NULL, "Error loading sources: %s", get_gerror_message(error));
        g_clear_error(&error);
        goto done;
    }

    config = config_data_new();
    if (config_file == NULL)
    {
        config_file = config_data_find_file();
    }
    if (config_file != NULL)
    {
        config_data_read_file(config, config_file);
    }
    if (option_diff_command != NULL)
    {
        g_free(config->diff_command_line);
        config->diff_command_line = option_diff_command;
    }
    win = create_main_window(config, sources);
    g_ptr_array_unref(sources);

    gtk_main();

    if (config_file != NULL)
    {
        config_data_write_file(config, config_file);
    }
    ret = 0;

done:
    g_strfreev(paths);
    g_free(config_file);
    diff_tree_config_unref(config);
    cleanup_main_window(win);

    return ret;
}
