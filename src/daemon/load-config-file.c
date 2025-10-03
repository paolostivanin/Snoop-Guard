#include <glib.h>
#include "main.h"
#include "../common.h"

static void set_default_values (gulong check_interval, gint notification_timeout, const gchar *mic_name, ConfigValues *cv);

ConfigValues *
load_config_file ()
{
    GError *err = NULL;
    ConfigValues *config_values = g_new0 (ConfigValues, 1);
    config_values->allow_list = NULL;
    config_values->deny_list = NULL;

    GKeyFile *conf_file = g_key_file_new ();

    gchar *config_file_path = g_strconcat (g_get_home_dir(), "/.config/", CONFIG_FILE_NAME, NULL);
    if (!g_key_file_load_from_file (conf_file, config_file_path, G_KEY_FILE_NONE, &err)) {
        g_printerr ("%s\nUsing default values.\n", err->message);
        set_default_values (DEFAULT_CHECK_INTERVAL, DEFAULT_NOTIFICATION_TIMEOUT, DEFAULT_MIC_NAME, config_values);
        g_clear_error (&err);
        g_free (config_file_path);
        g_key_file_free (conf_file);
        return config_values;
    }
    g_free (config_file_path);

    if (!g_key_file_has_group (conf_file, "server")) {
        g_printerr ("Couldn't find the group [server] inside the config file. Using default values.\n");
        set_default_values (DEFAULT_CHECK_INTERVAL, DEFAULT_NOTIFICATION_TIMEOUT, DEFAULT_MIC_NAME, config_values);
    }
    else {
        config_values->check_interval = g_key_file_get_uint64 (conf_file, "server", "check_interval", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (DEFAULT_CHECK_INTERVAL, 0, NULL, config_values);
        }

        config_values->notification_timeout = g_key_file_get_integer (conf_file, "server", "notification_timeout", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (0, DEFAULT_NOTIFICATION_TIMEOUT, NULL, config_values);
        }

        config_values->microphone_device = g_key_file_get_string (conf_file, "server", "microphone_device", &err);
        if (err != NULL) {
            g_printerr ("%s\nUsing default value.\n", err->message);
            g_clear_error (&err);
            set_default_values (0, 0, DEFAULT_MIC_NAME, config_values);
        }

        // policy lists
        config_values->allow_list = g_key_file_get_string_list (conf_file, "policy", "allow_list", NULL, &err);
        if (err != NULL) {
            g_clear_error (&err);
        }
        config_values->deny_list = g_key_file_get_string_list (conf_file, "policy", "deny_list", NULL, &err);
        if (err != NULL) {
            g_clear_error (&err);
        }
    }

    g_key_file_free (conf_file);

    if (config_values->check_interval <= 5) {
        g_printerr ("You have chosen a too short interval (less than 5 seconds). Falling back to the default value.\n");
        set_default_values (DEFAULT_CHECK_INTERVAL, 0, NULL, config_values);
    }
    if (config_values->notification_timeout < 0) {
        g_printerr("notification_timeout value can't be a negative number. Falling back to the default value.\n");
        set_default_values (0, DEFAULT_NOTIFICATION_TIMEOUT, NULL, config_values);
    }

    return config_values;
}


static void
set_default_values (gulong ci, gint nt, const gchar *mn, ConfigValues *cv)
{
    if (ci > 5)
        cv->check_interval = ci;

    if (nt > 0)
        cv->notification_timeout = nt;

    if (mn != NULL)
        cv->microphone_device = g_strdup (mn);
}