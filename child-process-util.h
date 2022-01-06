#ifndef CHILD_PROCESS_UTIL_H
#define CHILD_PROCESS_UTIL_H

/**
 * \file
 *
 * Functions to keep track of the child processes for running a diff tool.
 */

#include <glib.h>
#include <gio/gio.h>

#include "diff-tree-model.h"
#include "source-helpers.h"

G_BEGIN_DECLS

typedef struct _DtDiffProcessManager DtDiffProcessManager;

/**
 * Creates a DtDiffProcessManager.
 *
 * \param model The model to get files from.
 * \param diff_command The command line for an external diff tool. Filenames
 * are appended to this.
 */
DtDiffProcessManager *dt_diff_process_manager_new(DtDiffTreeModel *model,
        const char *diff_command, gboolean keep_process_files);

/**
 * Destroys a DtDiffProcessManager object.
 *
 * This will delete all of the temp files, but won't kill any child processes
 * that are already running.
 */
void dt_diff_process_manager_free(DtDiffProcessManager *manager);

/**
 * Starts the diff tool for a row in the DtDiffTreeModel.
 *
 * If a child process is already running for this row, then this will not start
 * a new one.
 *
 * \param manager The DtDiffProcessManager.
 * \param iter The row to display.
 * \param error Returns an error code.
 * \return TRUE on success, FALSE on failure.
 */
gboolean dt_diff_process_manager_start_diff(DtDiffProcessManager *manager, GtkTreeIter *iter, GError **error);

G_END_DECLS

#endif // CHILD_PROCESS_UTIL_H
