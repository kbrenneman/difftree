#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <gtk/gtk.h>

#include "app-config.h"

G_BEGIN_DECLS

/**
 * Displays a settings dialog to edit the configuration.
 *
 * \param parent The parent window for the dialog.
 * \param config The DiffTreeConfig to edit.
 */
void dt_settings_editor_show_dialog(GtkWindow *parent, DiffTreeConfig *config);

G_END_DECLS

#endif // SETTINGS_WINDOW_H
