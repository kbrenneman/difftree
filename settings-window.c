#include "settings-window.h"

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib.h>

#include "app-config.h"

typedef struct
{
    GtkEntry *diff_command_entry;
    GtkCheckButton *keep_temp_files_button;
} DtSettingsEditorData;

G_DEFINE_QUARK(DT_SETTINGS_EDITOR_DATA, dt_settings_editor_data);

static GtkWidget *dt_settings_editor_create(DiffTreeConfig *config)
{
    DtSettingsEditorData *data = g_malloc(sizeof(DtSettingsEditorData));
    GtkGrid *content = GTK_GRID(gtk_grid_new());
    GtkLabel *label;

    g_object_set_qdata_full(G_OBJECT(content), dt_settings_editor_data_quark(), data, g_free);

    data->diff_command_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_show(GTK_WIDGET(data->diff_command_entry));
    if (config->diff_command_line != NULL)
    {
        gtk_entry_set_text(data->diff_command_entry, config->diff_command_line);
    }
    gtk_entry_set_width_chars(data->diff_command_entry, 50);
    gtk_entry_set_activates_default(data->diff_command_entry, TRUE);

    data->keep_temp_files_button = GTK_CHECK_BUTTON(
            gtk_check_button_new_with_mnemonic("_Keep temp files"));
    gtk_widget_show(GTK_WIDGET(data->keep_temp_files_button));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->keep_temp_files_button),
            config->keep_temp_files);

    label = GTK_LABEL(gtk_label_new_with_mnemonic("_Diff command:"));
    gtk_widget_show(GTK_WIDGET(label));

    gtk_label_set_mnemonic_widget(label, GTK_WIDGET(data->diff_command_entry));
    gtk_grid_attach(content, GTK_WIDGET(label), 0, 0, 1, 1);
    gtk_grid_attach(content, GTK_WIDGET(data->diff_command_entry), 1, 0, 1, 1);
    gtk_grid_attach(content, GTK_WIDGET(data->keep_temp_files_button), 0, 1, 1, 2);

    return GTK_WIDGET(content);
}

static void dt_settings_editor_save(GtkWidget *editor, DiffTreeConfig *config)
{
    DtSettingsEditorData *data = g_object_get_qdata(G_OBJECT(editor), dt_settings_editor_data_quark());
    const gchar *str = gtk_entry_get_text(data->diff_command_entry);

    g_free(config->diff_command_line);
    config->diff_command_line = g_strdup(str != NULL ? str : "");
    config->keep_temp_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->keep_temp_files_button));
}

void dt_settings_editor_show_dialog(GtkWindow *parent, DiffTreeConfig *config)
{
    GtkDialog *dialog;
    GtkWidget *editor;
    gint response;

    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons("Settings", parent,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "OK", GTK_RESPONSE_OK,
            "Cancel", GTK_RESPONSE_CANCEL,
            NULL));
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);

    editor = dt_settings_editor_create(config);
    gtk_widget_show(GTK_WIDGET(editor));
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)), editor);

    response = gtk_dialog_run(dialog);
    if (response == GTK_RESPONSE_OK)
    {
        dt_settings_editor_save(editor, config);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
