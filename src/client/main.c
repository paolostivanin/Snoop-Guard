#include <gtk/gtk.h>
#include "main.h"

gint
main (gint argc, gchar **argv)
{
    /* TODO
     * - show current values taken from config file
     * - set new values
     */
    GtkApplication *app;
    gint status;

    GdkPixbuf *logo = create_logo (FALSE);

    if (logo != NULL)
        gtk_window_set_default_icon (logo);

    app = gtk_application_new ("org.gnome.gtkcrypto", G_APPLICATION_FLAGS_NONE);
    g_signal_connect (app, "startup", G_CALLBACK (startup), NULL);
    g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);

    status = g_application_run (G_APPLICATION (app), argc, argv);

    g_object_unref (app);

    return status;
}


void
startup (GtkApplication *application, gpointer __attribute__((__unused__)) data)
{
    static const GActionEntry actions[] = {
            {"about", about},
            {"quit",  quit}
    };

    const gchar *quit_accels[2] = {"<Ctrl>Q", NULL};

    g_action_map_add_action_entries (G_ACTION_MAP (application), actions, G_N_ELEMENTS (actions), application);

    gtk_application_set_accels_for_action (GTK_APPLICATION (application), "app.quit", quit_accels);

    GMenu *menu = g_menu_new ();

    GMenu *section = g_menu_new ();
    g_menu_append (section, "About", "app.about");
    g_menu_append_section (G_MENU (menu), NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    section = g_menu_new ();
    g_menu_append (section, "Quit", "app.quit");
    g_menu_append_section (G_MENU (menu), NULL, G_MENU_MODEL (section));
    g_object_unref (section);

    gtk_application_set_app_menu (application, G_MENU_MODEL (menu));
    g_object_unref (menu);
}


void
activate (GtkApplication *app,
          gpointer __attribute__((__unused__)) data)
{
    AppWidgets *widgets = g_new0 (AppWidgets, 1);

    widgets->main_window = create_main_window (app);
    gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (widgets->main_window));

    add_boxes_and_grid (widgets);

    gtk_widget_show_all(widgets->main_window);
}