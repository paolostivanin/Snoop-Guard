#include <glib.h>
#include "main.h"
#include "../common.h"

static gchar *
default_config_path (void)
{
    const gchar *xdg = g_getenv ("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return g_build_filename (xdg, CONFIG_FILE_NAME, NULL);
    }
    return g_build_filename (g_get_home_dir (), ".config", CONFIG_FILE_NAME, NULL);
}

static void
fill_defaults (ConfigValues *cv)
{
    cv->check_interval        = DEFAULT_CHECK_INTERVAL;
    cv->notification_timeout  = DEFAULT_NOTIFICATION_TIMEOUT;
    cv->log_max_bytes         = DEFAULT_LOG_MAX_BYTES;
    cv->microphone_device     = NULL;
    cv->allow_list            = NULL;
    cv->deny_list             = NULL;
    cv->mic_allow_list        = NULL;
    cv->mic_deny_list         = NULL;
}

static gchar *
get_string_or_null (GKeyFile *kf, const gchar *group, const gchar *key)
{
    GError *err = NULL;
    gchar *v = g_key_file_get_string (kf, group, key, &err);
    if (err) { g_clear_error (&err); return NULL; }
    if (v && !*v) { g_free (v); return NULL; }
    return v;
}

static gchar **
get_strv_or_null (GKeyFile *kf, const gchar *group, const gchar *key)
{
    GError *err = NULL;
    gchar **v = g_key_file_get_string_list (kf, group, key, NULL, &err);
    if (err) { g_clear_error (&err); return NULL; }
    return v;
}

void
config_values_free (ConfigValues *cv)
{
    if (!cv) return;
    g_free (cv->microphone_device);
    g_strfreev (cv->allow_list);
    g_strfreev (cv->deny_list);
    g_strfreev (cv->mic_allow_list);
    g_strfreev (cv->mic_deny_list);
    g_free (cv);
}

ConfigValues *
load_config_file (const gchar *override_path)
{
    ConfigValues *cv = g_new0 (ConfigValues, 1);
    fill_defaults (cv);

    gchar *path = override_path ? g_strdup (override_path) : default_config_path ();
    GKeyFile *kf = g_key_file_new ();
    GError *err = NULL;

    if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, &err)) {
        g_message ("No config file at %s; using defaults (%s)",
                   path, err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        g_key_file_free (kf);
        g_free (path);
        return cv;
    }

    if (g_key_file_has_group (kf, "server")) {
        guint64 ci = g_key_file_get_uint64 (kf, "server", "check_interval", &err);
        if (err) { g_clear_error (&err); }
        else      { cv->check_interval = ci; }

        gint nt = g_key_file_get_integer (kf, "server", "notification_timeout", &err);
        if (err) { g_clear_error (&err); }
        else      { cv->notification_timeout = nt; }

        guint64 lmb = g_key_file_get_uint64 (kf, "server", "log_max_bytes", &err);
        if (err) { g_clear_error (&err); }
        else if (lmb > 0) { cv->log_max_bytes = (gsize) lmb; }

        cv->microphone_device = get_string_or_null (kf, "server", "microphone_device");
    }

    if (g_key_file_has_group (kf, "policy")) {
        cv->allow_list     = get_strv_or_null (kf, "policy", "allow_list");
        cv->deny_list      = get_strv_or_null (kf, "policy", "deny_list");
        cv->mic_allow_list = get_strv_or_null (kf, "policy", "mic_allow_list");
        cv->mic_deny_list  = get_strv_or_null (kf, "policy", "mic_deny_list");
    }

    g_key_file_free (kf);
    g_free (path);

    /* Validate / clamp. */
    if (cv->check_interval < MIN_CHECK_INTERVAL) {
        g_warning ("check_interval=%" G_GUINT64_FORMAT " too small; using default %d",
                   cv->check_interval, DEFAULT_CHECK_INTERVAL);
        cv->check_interval = DEFAULT_CHECK_INTERVAL;
    }
    if (cv->notification_timeout < 0) {
        g_warning ("notification_timeout < 0; using default %d", DEFAULT_NOTIFICATION_TIMEOUT);
        cv->notification_timeout = DEFAULT_NOTIFICATION_TIMEOUT;
    }
    return cv;
}
