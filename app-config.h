#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/**
 * \file
 *
 * Handles settings stored in a config file.
 */

#include <glib.h>
#include "ref-count-struct.h"

G_BEGIN_DECLS

/**
 * A struct with the various configuration options.
 */
typedef struct
{
    UtilRefCountedBase refcount;

    gint window_width;
    gint window_height;
    char *diff_command_line;
    gboolean keep_temp_files;
} DiffTreeConfig;

UTIL_DECLARE_BOXED_REFCOUNT_FUNCS(DiffTreeConfig, diff_tree_config)

/**
 * Allocates and initializes a new DiffTreeConfig struct.
 */
DiffTreeConfig *config_data_new(void);

/**
 * Searches for a default config file to use.
 *
 * Note that the config file may or may not exist.
 *
 * The returned path should be freed with g_free.
 */
char *config_data_find_file(void);

/**
 * Reads a config file, if one exists.
 */
void config_data_read_file(DiffTreeConfig *config, const char *filename);

/**
 * Writes the config file.
 */
void config_data_write_file(DiffTreeConfig *config, const char *filename);

G_END_DECLS

#endif // APP_CONFIG_H
