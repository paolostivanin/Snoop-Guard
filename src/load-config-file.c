#include <glib.h>
#include "webmonit.h"

static void set_default_values (gint check_interval, gint notification_timeout, gchar *mic_name, ConfigValues *cv);

ConfigValues *
load_config_file ()
{
    GError *err = NULL;
    ConfigValues *config_values = g_new0 (ConfigValues, 1);

    GKeyFile *conf_file = g_key_file_new ();

    if (!g_key_file_load_from_file (conf_file, "PATH", G_KEY_FILE_NONE, &err)) { // TODO think about a path
        g_printerr ("%s\nUsing default values.\n", err->message);
        set_default_values (DEFAULT_CHECK_INTERVAL, DEFAULT_NOTIFICATION_TIMEOUT, DEFAULT_MIC_NAME, config_values);
        g_clear_error (&err);
        g_key_file_free (conf_file);
        return config_values;
    }

    if (!g_key_file_has_group (conf_file, "server")) {
        g_printerr ("Couldn't find the group [server] inside the config file. Using default values.\n");
        set_default_values (DEFAULT_CHECK_INTERVAL, DEFAULT_NOTIFICATION_TIMEOUT, DEFAULT_MIC_NAME, config_values);
    }
    else {
        config_values->check_interval = g_key_file_get_integer (conf_file, "server", "check_interval", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (DEFAULT_CHECK_INTERVAL, -1, NULL, config_values);
        }

        config_values->notification_timeout = g_key_file_get_integer (conf_file, "server", "notification_timeout", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (-1, DEFAULT_NOTIFICATION_TIMEOUT, NULL, config_values);
        }

        config_values->microphone_device = g_key_file_get_string (conf_file, "server", "microphone_device", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (-1, -1, DEFAULT_MIC_NAME, config_values);
        }

        config_values->ignore_apps = g_key_file_get_string_list (conf_file, "server", "ignore_apps", NULL, &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            // default value is NULL which is ok because in case of an error NULL is returned.
        }
    }

    g_key_file_free (conf_file);

    // TODO check that values are usable (int > 0, etc)
    // TODO g_free err_msg if set + g_free microphone_device + g_strfreev ignore_apps + g_free config_values
    return config_values;
}


static void
set_default_values (gint ci, gint nt, gchar *mn, ConfigValues *cv)
{
    if (ci > 0)
        cv->check_interval = ci;

    if (nt > 0)
        cv->notification_timeout = nt;

    if (mn != NULL)
        cv->microphone_device = g_strdup (mn);
}