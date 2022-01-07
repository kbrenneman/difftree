#include "child-process-util.h"

#define READ_BUF_SIZE 1024

struct _DtDiffProcessManager
{
    DtDiffTreeModel *model;
    GTree *processes;

    gboolean keep_process_files;
};

typedef struct
{
    GPid pid;

    /**
     * The source ID for waiting for a child to terminate, or zero if
     * a child process isn't running.
     */
    guint child_watch_id;
    DtFileKey *key;
    DtDiffProcessManager *owner;

    gboolean init_files_done;
    gint num_files;
    gchar **files;

    /**
     * If true, then keep the temp files after a child process terminates,
     * and instead delete them all when the DtDiffProcessManager is destroyed.
     *
     * This would be important if we're using a diff tool that uses a single
     * process to handle multiple invocations.
     */
    gboolean keep_temp_files;
    gint num_temp_files;
    GFile **temp_files;
} DtDiffProcess;

static void cleanup_files(DtDiffProcess *proc)
{
    gint i;

    for (i=0; i<proc->num_temp_files; i++)
    {
        GError *error = NULL;
        g_debug("Deleting temp file: %s", g_file_peek_path(proc->temp_files[i]));
        if (!g_file_delete(proc->temp_files[i], NULL, &error))
        {
            g_critical("Can't delete temp file %s: %s\n",
                    g_file_peek_path(proc->temp_files[i]), error->message);
            g_clear_error(&error);
        }
        g_clear_object(&proc->temp_files[i]);
    }
    proc->num_temp_files = 0;

    for (i=0; i<proc->num_files; i++)
    {
        g_free(proc->files[i]);
        proc->files[i] = NULL;
    }
    proc->num_files = 0;
    proc->init_files_done = FALSE;
}

static void dt_diff_process_free(DtDiffProcess *proc)
{
    if (proc != NULL)
    {
        if (proc->child_watch_id > 0)
        {
            g_source_remove(proc->child_watch_id);
            proc->child_watch_id = 0;
            g_spawn_close_pid(proc->pid);
        }
        cleanup_files(proc);
        dt_file_key_unref(proc->key);
        g_free(proc->files);
        g_free(proc->temp_files);
        g_free(proc);
    }
}

DtDiffProcessManager *dt_diff_process_manager_new(DtDiffTreeModel *model)
{
    DtDiffProcessManager *manager = g_malloc0(sizeof(DtDiffProcessManager));

    manager->model = g_object_ref(model);
    manager->processes = g_tree_new_full(compare_3to2, dt_file_key_compare,
            (GDestroyNotify) dt_file_key_unref, (GDestroyNotify) dt_diff_process_free);
    return manager;
}

void dt_diff_process_manager_free(DtDiffProcessManager *manager)
{
    if (manager != NULL)
    {
        g_tree_unref(manager->processes);
        g_object_unref(manager->model);
        g_free(manager);
    }
}

static DtDiffProcess *lookup_process(DtDiffProcessManager *manager, GtkTreeIter *iter)
{
    DtFileKey *key;
    DtDiffProcess *proc;

    key = dt_file_key_from_model(GTK_TREE_MODEL(manager->model), iter);
    proc = g_tree_lookup(manager->processes, key);
    if (proc == NULL)
    {
        gint num_sources = dt_diff_tree_model_get_num_sources(manager->model);
        proc = g_malloc0(sizeof(DtDiffProcess));
        proc->child_watch_id = 0;
        proc->key = dt_file_key_ref(key);
        proc->owner = manager;
        proc->files = g_malloc0(num_sources * sizeof(GFile *));
        proc->temp_files = g_malloc0(num_sources * sizeof(GFile *));
        g_tree_insert(manager->processes, dt_file_key_ref(key), proc);
    }
    dt_file_key_unref(key);
    return proc;
}

static GFile *create_temp_file(const char *filename, GInputStream *stream, GError **error)
{
    gchar *name_template = g_strdup_printf("difftree-XXXXXX-%s", filename);
    GFileIOStream *iostream = NULL;
    GOutputStream *outstream = NULL;
    GFile *gf = NULL;
    gboolean success = TRUE;

    gf = g_file_new_tmp(name_template, &iostream, error);
    g_free(name_template);
    if (gf == NULL)
    {
        return NULL;
    }

    g_debug("Writing temp file: %s -> %s", filename, g_file_peek_path(gf));

    outstream = g_io_stream_get_output_stream(G_IO_STREAM(iostream));
    while (TRUE)
    {
        char buf[READ_BUF_SIZE];
        gssize num;

        num = g_input_stream_read(stream, buf, sizeof(buf), NULL, error);
        if (num == 0)
        {
            break;
        }
        else if (num < 0)
        {
            g_prefix_error(error, "Failed to read from source: ");
            success = FALSE;
            break;
        }

        if (!g_output_stream_write_all(outstream, buf, num, NULL, NULL, error))
        {
            g_prefix_error(error, "Failed to write to temp file: ");
            success = FALSE;
            break;
        }
    }

    if (!g_io_stream_close(G_IO_STREAM(iostream), NULL, error))
    {
        g_prefix_error(error, "Failed to close temp file: ");
        success = FALSE;
    }
    g_object_unref(iostream);

    if (!success)
    {
        GError *delerr = NULL;
        g_debug("Failed to write temp file -- deleting: %s", g_file_peek_path(gf));
        if (!g_file_delete(gf, NULL, &delerr))
        {
            g_critical("Failed to delete temp file: %s\n", delerr->message);
            g_clear_error(&delerr);
        }
        g_object_unref(gf);
        gf = NULL;
    }
    return gf;
}

static gboolean init_process_files(DtDiffProcessManager *manager, DtDiffProcess *proc, GtkTreeIter *iter, GError **error)
{
    typedef struct
    {
        DtTreeSource *source;
        DtTreeSourceNode *node;
    } SourceFile;

    gint numFiles = 0;
    SourceFile *files;
    gboolean success = TRUE;
    gint i;

    if (proc->init_files_done)
    {
        return TRUE;
    }

    // Try to find a GtkTreeIter from each source.
    files = g_malloc(dt_diff_tree_model_get_num_sources(manager->model) * sizeof(SourceFile));
    for (i=0; i<dt_diff_tree_model_get_num_sources(manager->model); i++)
    {
        DtTreeSourceNode *node = dt_diff_tree_model_get_source_node(manager->model, i, iter);
        if (node)
        {
            files[numFiles].source = dt_diff_tree_model_get_source(manager->model, i);
            files[numFiles].node = node;
            numFiles++;
        }
    }
    if (numFiles < 2)
    {
        g_free(files);
        // This isn't an error, so return success here. The caller will check
        // the number of files.
        return TRUE;
    }

    for (i=0; i<numFiles; i++)
    {
        GFileInfo *info = NULL;
        GFile *fspath = NULL;

        info = dt_tree_source_get_file_info(files[i].source, files[i].node);

        if (g_file_info_get_file_type(info) != G_FILE_TYPE_SYMBOLIC_LINK)
        {
            fspath = G_FILE(g_file_info_get_attribute_object(info, DT_FILE_ATTRIBUTE_FS_PATH));
        }

        if (fspath != NULL)
        {
            proc->files[proc->num_files] = g_file_get_path(fspath);;
        }
        else
        {
            // TODO: Do this asynchronously
            GInputStream *stream;
            GFile *tempfile;

            if (g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR)
            {
                stream = dt_tree_source_open_file(files[i].source, files[i].node, NULL, error);
                if (stream == NULL)
                {
                    success = FALSE;
                    break;
                }
            }
            else
            {
                stream = g_memory_input_stream_new();
                if (g_file_info_get_file_type(info) == G_FILE_TYPE_SYMBOLIC_LINK)
                {
                    const gchar *target = g_file_info_get_symlink_target(info);
                    if (target != NULL)
                    {
                        g_memory_input_stream_add_data(G_MEMORY_INPUT_STREAM(stream),
                                g_strdup(target), -1, g_free);
                    }
                }
            }

            tempfile = create_temp_file(g_file_info_get_name(info), stream, error);
            g_object_unref(stream);
            if (tempfile == NULL)
            {
                success = FALSE;
                break;
            }

            proc->temp_files[proc->num_temp_files] = tempfile;
            proc->num_temp_files++;
            proc->files[proc->num_files] = g_file_get_path(tempfile);
        }
        proc->num_files++;
    }

    g_free(files);
    if (!success || proc->num_files < 2)
    {
        cleanup_files(proc);
    }
    else
    {
        proc->init_files_done = TRUE;
    }
    return success;
}

static void on_child_exit(GPid pid, gint status, gpointer userdata)
{
    DtDiffProcess *proc = userdata;

    g_debug("Child %" G_PID_FORMAT " exited with %d", pid, status);

    g_assert(proc->child_watch_id != 0);
    proc->child_watch_id = 0;
    g_spawn_close_pid(proc->pid);

    if (!proc->keep_temp_files)
    {
        DtFileKey *key = dt_file_key_ref(proc->key);
        g_tree_remove(proc->owner->processes, key);
        dt_file_key_unref(key);
    }
}

gboolean dt_diff_process_manager_start_diff(DtDiffProcessManager *manager,
        const char *diff_command, gboolean keep_temp_files,
        GtkTreeIter *iter, GError **error)
{
    DtDiffProcess *proc = NULL;
    gchar **argv = NULL;
    gint argc = 0;
    gint i;
    gboolean success = FALSE;

    if (!g_shell_parse_argv(diff_command, &argc, &argv, error))
    {
        goto done;
    }
    if (argc < 1)
    {
        g_warning("No diff command set.\n");
        success = TRUE;
        goto done;
    }

    proc = lookup_process(manager, iter);
    if (proc->child_watch_id != 0)
    {
        g_debug("Child process is already running");
        success = TRUE;
        goto done;
    }
    if (!init_process_files(manager, proc, iter, error))
    {
        goto done;
    }
    if (proc->num_files < 2)
    {
        success = TRUE;
        goto done;
    }

    // Allocate enough space to append the filenames
    argv = g_realloc(argv, (argc + proc->num_files + 1) * sizeof(gchar *));
    for (i=0; i<proc->num_files; i++)
    {
        argv[argc++] = g_strdup(proc->files[i]);
    }
    argv[argc] = NULL;

    g_debug("Starting child process:");
    for (i=0; i<argc; i++)
    {
        g_debug("   argv[%d] = %s", i, argv[i]);
    }

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &proc->pid, error))
    {
        goto done;
    }
    g_debug("Started child %" G_PID_FORMAT, proc->pid);

    proc->child_watch_id = g_child_watch_add(proc->pid, on_child_exit, proc);
    if (keep_temp_files)
    {
        proc->keep_temp_files = TRUE;
    }
    success = TRUE;

done:
    g_strfreev(argv);
    if (!success)
    {
        // If keep_temp_files is true, then we succesfully started a diff
        // process at least once, so we should leave the temp files in place.
        // Otherwise, remove them.
        if (proc != NULL && !proc->keep_temp_files)
        {
            DtFileKey *key = dt_file_key_ref(proc->key);
            g_tree_remove(proc->owner->processes, key);
            dt_file_key_unref(key);
        }
    }
    return TRUE;
}

