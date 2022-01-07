#include "app-config.h"

#include <unistd.h>

static const char *DEFAULT_CONFIG_FILENAME = "difftree.conf";

static const gint DEFAULT_WINDOW_WIDTH = 500;
static const gint DEFAULT_WINDOW_HEIGHT = 500;
static const gchar *DEFAULT_DIFF_COMMAND_LINE = "/usr/bin/diff";
static const gboolean DEFAULT_KEEP_TEMP_FILES = FALSE;

static void config_data_free(DiffTreeConfig *config);

UTIL_DEFINE_BOXED_REFCOUNT_TYPE(DiffTreeConfig, diff_tree_config, config_data_free)

DiffTreeConfig *config_data_new(void)
{
    DiffTreeConfig *config = g_malloc0(sizeof(DiffTreeConfig));
    config->window_width = DEFAULT_WINDOW_WIDTH;
    config->window_height = DEFAULT_WINDOW_HEIGHT;
    config->diff_command_line = g_strdup(DEFAULT_DIFF_COMMAND_LINE);
    config->keep_temp_files = DEFAULT_KEEP_TEMP_FILES;

    return diff_tree_config_ref(config);
}

void config_data_free(DiffTreeConfig *config)
{
    if (config != NULL)
    {
        g_free(config);
    }
}

static void set_default_values(GKeyFile *keyfile)
{
    GError *error = NULL;
    gchar *str;

    g_key_file_get_integer(keyfile, "main", "window_width", &error);
    if (error != NULL)
    {
        g_clear_error(&error);
        g_key_file_set_integer(keyfile, "main", "window_width", DEFAULT_WINDOW_WIDTH);
    }

    g_key_file_get_integer(keyfile, "main", "window_height", &error);
    if (error != NULL)
    {
        g_clear_error(&error);
        g_key_file_set_integer(keyfile, "main", "window_height", DEFAULT_WINDOW_HEIGHT);
    }

    str = g_key_file_get_string(keyfile, "main", "diff_command_line", &error);
    if (str == NULL || error != NULL)
    {
        const gchar *comment = 
            " The command line to use to display the diff for a particular file. The\n"
            " filenames are appended to the command line.";
        g_clear_error(&error);
        g_key_file_set_string(keyfile, "main", "diff_command_line", DEFAULT_DIFF_COMMAND_LINE);
        g_key_file_set_comment(keyfile, "main", "diff_command_line", comment, NULL);
    }
    g_free(str);

    g_key_file_get_boolean(keyfile, "main", "keep_temp_files", &error);
    if (error != NULL)
    {
        const gchar *comment = 
            " If this is true, then keep temp files around after the child process\n"
            " terminates. Might be needed if the tool in diff_command_line reuses a single\n"
            " process when you run it multiple times.";
        g_clear_error(&error);
        g_key_file_set_boolean(keyfile, "main", "keep_temp_files", DEFAULT_KEEP_TEMP_FILES);
        g_key_file_set_comment(keyfile, "main", "keep_temp_files", comment, NULL);
    }
}

static void update_from_keyfile(DiffTreeConfig *config, GKeyFile *keyfile)
{
    GError *err = NULL;
    gint ival;
    gchar *str;
    gboolean bval;

    ival = g_key_file_get_integer(keyfile, "main", "window_width", NULL);
    if (ival > 0)
    {
        config->window_width = ival;
    }

    ival = g_key_file_get_integer(keyfile, "main", "window_height", NULL);
    if (ival > 0)
    {
        config->window_height = ival;
    }

    str = g_key_file_get_string(keyfile, "main", "diff_command_line", NULL);
    if (str != NULL)
    {
        g_free(config->diff_command_line);
        config->diff_command_line = str;
    }

    bval = g_key_file_get_boolean(keyfile, "main", "keep_temp_files", &err);
    if (err != NULL)
    {
        g_clear_error(&err);
    }
    else
    {
        config->keep_temp_files = bval;
    }
}

/**
 * Updates a GKeyFile with the values in \p config.
 *
 * \return TRUE if the config data has changed.
 */
static gboolean store_to_keyfile(DiffTreeConfig *config, GKeyFile *keyfile)
{
    gboolean changed = FALSE;
    gchar *str;

    changed = changed || (g_key_file_get_integer(keyfile, "main", "window_width", NULL) != config->window_width);
    changed = changed || (g_key_file_get_integer(keyfile, "main", "window_height", NULL) != config->window_height);
    changed = changed || (g_key_file_get_boolean(keyfile, "main", "keep_temp_files", NULL) != config->keep_temp_files);

    str = g_key_file_get_string(keyfile, "main", "diff_command_line", NULL);
    if (g_strcmp0(str, config->diff_command_line) != 0)
    {
        changed = TRUE;
        if (config->diff_command_line != NULL)
        {
            g_key_file_set_string(keyfile, "main", "diff_command_line", config->diff_command_line);
        }
        else
        {
            g_key_file_set_string(keyfile, "main", "diff_command_line", "");
        }
    }
    g_free(str);

    if (changed)
    {
        g_key_file_set_integer(keyfile, "main", "window_width", config->window_width);
        g_key_file_set_integer(keyfile, "main", "window_height", config->window_height);
        g_key_file_set_boolean(keyfile, "main", "keep_temp_files", config->keep_temp_files);
    }
    else
    {
        g_debug("Config file hasn't changed");
    }
    return changed;
}

char *config_data_find_file(void)
{
    const char *dir;

    if (DEFAULT_CONFIG_FILENAME == NULL)
    {
        return NULL;
    }

    // Look in the current directory first.
    if (access(DEFAULT_CONFIG_FILENAME, R_OK) == 0)
    {
        //g_debug("Using config file: %s\n", DEFAULT_CONFIG_FILENAME);
        return g_strdup(DEFAULT_CONFIG_FILENAME);
    }

    // If that fails, then try the user config directory.
    dir = g_get_user_config_dir();
    if (dir != NULL)
    {
        gchar *path = g_build_filename(dir, DEFAULT_CONFIG_FILENAME, NULL);
        return path;
    }

    return NULL;
}

void config_data_read_file(DiffTreeConfig *config, const char *filename)
{
    if (filename != NULL)
    {
        GKeyFile *keyfile = g_key_file_new();
        g_debug("Reading config file from %s", filename);
        g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_KEEP_COMMENTS, NULL);
        update_from_keyfile(config, keyfile);
        g_key_file_free(keyfile);
    }
}

void config_data_write_file(DiffTreeConfig *config, const char *filename)
{
    GKeyFile *keyfile;

    if (filename == NULL)
    {
        return;
    }

    keyfile = g_key_file_new();

    // Re-read the current file, and then add default values for any keys that
    // are missing.
    g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_KEEP_COMMENTS, NULL);
    set_default_values(keyfile);

    // Now, store the updated values, and if any of them are different from the
    // config file, rewrite the file.
    if (store_to_keyfile(config, keyfile))
    {
        GError *error = NULL;

        g_debug("Writing config file to %s", filename);
        if (!g_key_file_save_to_file(keyfile, filename, &error))
        {
            g_warning("Can't save config file: %s\n", error->message);
            g_clear_error(&error);
        }
    }
}
