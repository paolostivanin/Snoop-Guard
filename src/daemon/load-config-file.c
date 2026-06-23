#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include "main.h"
#include "../common.h"

#define MAX_CONFIG_BYTES ((goffset) 1024 * 1024)
#define MAX_POLICY_ENTRIES 256
#define MAX_POLICY_ENTRY_BYTES 256

static GQuark
config_error_quark (void)
{
    return g_quark_from_static_string ("snoop-guard-config-error");
}

static gchar *
default_config_path (void)
{
    return g_build_filename (g_get_user_config_dir (), CONFIG_FILE_NAME, NULL);
}

static ConfigValues *
config_values_new_defaults (void)
{
    ConfigValues *cv = g_new0 (ConfigValues, 1);
    cv->check_interval = DEFAULT_CHECK_INTERVAL;
    cv->notification_timeout = DEFAULT_NOTIFICATION_TIMEOUT;
    cv->log_max_bytes = DEFAULT_LOG_MAX_BYTES;
    return cv;
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

static gboolean
validate_list (gchar **values, const gchar *key, GError **error)
{
    guint count = g_strv_length (values);
    if (count > MAX_POLICY_ENTRIES) {
        g_set_error (error, config_error_quark (), 1,
                     "%s contains more than %u entries", key, MAX_POLICY_ENTRIES);
        return FALSE;
    }
    for (guint i = 0; values && values[i]; i++) {
        g_strstrip (values[i]);
        gboolean has_control = FALSE;
        for (const guchar *p = (const guchar *) values[i]; *p; p++) {
            if (*p < 0x20 || *p == 0x7f) {
                has_control = TRUE;
                break;
            }
        }
        if (!*values[i] || strlen (values[i]) > MAX_POLICY_ENTRY_BYTES ||
            has_control ||
            !g_utf8_validate (values[i], -1, NULL)) {
            g_set_error (error, config_error_quark (), 1,
                         "%s contains an empty, oversized, or invalid UTF-8 entry", key);
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
get_uint64_if_present (GKeyFile *kf,
                       const gchar *group,
                       const gchar *key,
                       guint64 *value,
                       GError **error)
{
    if (!g_key_file_has_key (kf, group, key, NULL)) return TRUE;
    GError *local = NULL;
    guint64 parsed = g_key_file_get_uint64 (kf, group, key, &local);
    if (local) {
        g_propagate_prefixed_error (error, local, "%s: ", key);
        return FALSE;
    }
    *value = parsed;
    return TRUE;
}

static gboolean
get_int_if_present (GKeyFile *kf,
                    const gchar *group,
                    const gchar *key,
                    gint *value,
                    GError **error)
{
    if (!g_key_file_has_key (kf, group, key, NULL)) return TRUE;
    GError *local = NULL;
    gint parsed = g_key_file_get_integer (kf, group, key, &local);
    if (local) {
        g_propagate_prefixed_error (error, local, "%s: ", key);
        return FALSE;
    }
    *value = parsed;
    return TRUE;
}

static gchar *
get_optional_string (GKeyFile *kf, const gchar *group, const gchar *key)
{
    if (!g_key_file_has_key (kf, group, key, NULL)) return NULL;
    gchar *value = g_key_file_get_string (kf, group, key, NULL);
    if (value) g_strstrip (value);
    if (value && !*value) g_clear_pointer (&value, g_free);
    return value;
}

static gchar **
get_optional_list (GKeyFile *kf, const gchar *key, GError **error)
{
    if (!g_key_file_has_key (kf, "policy", key, NULL)) return NULL;
    GError *local = NULL;
    gchar **values = g_key_file_get_string_list (kf, "policy", key, NULL, &local);
    if (local) {
        g_propagate_prefixed_error (error, local, "%s: ", key);
        return NULL;
    }
    if (!validate_list (values, key, error)) {
        g_strfreev (values);
        return NULL;
    }
    return values;
}

static gboolean
contains_ascii_control (const gchar *value)
{
    for (const guchar *p = (const guchar *) value; value && *p; p++) {
        if (*p < 0x20 || *p == 0x7f) return TRUE;
    }
    return FALSE;
}

ConfigValues *
load_config_file (const gchar *override_path, gboolean explicit_path, GError **error)
{
    gchar *path = override_path ? g_strdup (override_path) : default_config_path ();
    GStatBuf st;
    if (g_stat (path, &st) != 0) {
        if (!explicit_path && errno == ENOENT) {
            g_message ("No config file at %s; using defaults", path);
            g_free (path);
            return config_values_new_defaults ();
        }
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     "Cannot access config %s: %s", path, g_strerror (errno));
        g_free (path);
        return NULL;
    }
    if (st.st_size < 0 || st.st_size > MAX_CONFIG_BYTES) {
        g_set_error (error, config_error_quark (), 1,
                     "Config %s exceeds the %" G_GOFFSET_FORMAT "-byte limit",
                     path, MAX_CONFIG_BYTES);
        g_free (path);
        return NULL;
    }

    GKeyFile *kf = g_key_file_new ();
    GError *local = NULL;
    if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, &local)) {
        g_propagate_prefixed_error (error, local, "Cannot parse config %s: ", path);
        g_key_file_unref (kf);
        g_free (path);
        return NULL;
    }

    ConfigValues *cv = config_values_new_defaults ();
    guint64 interval = cv->check_interval;
    guint64 log_bytes = cv->log_max_bytes;
    if (!get_uint64_if_present (kf, "server", "check_interval", &interval, error) ||
        !get_int_if_present (kf, "server", "notification_timeout",
                            &cv->notification_timeout, error) ||
        !get_uint64_if_present (kf, "server", "log_max_bytes", &log_bytes, error)) {
        goto fail;
    }
    if (interval < MIN_CHECK_INTERVAL || interval > MAX_CHECK_INTERVAL) {
        g_set_error (error, config_error_quark (), 1,
                     "check_interval must be between %d and %d",
                     MIN_CHECK_INTERVAL, MAX_CHECK_INTERVAL);
        goto fail;
    }
    if (cv->notification_timeout < 0 ||
        cv->notification_timeout > MAX_NOTIFICATION_TIMEOUT) {
        g_set_error (error, config_error_quark (), 1,
                     "notification_timeout must be between 0 and %d",
                     MAX_NOTIFICATION_TIMEOUT);
        goto fail;
    }
    if (log_bytes == 0) log_bytes = DEFAULT_LOG_MAX_BYTES;
    if (log_bytes > MAX_LOG_BYTES || log_bytes > G_MAXSIZE) {
        g_set_error (error, config_error_quark (), 1,
                     "log_max_bytes is too large");
        goto fail;
    }
    cv->check_interval = interval;
    cv->log_max_bytes = (gsize) log_bytes;
    cv->microphone_device = get_optional_string (kf, "server", "microphone_device");
    if (cv->microphone_device &&
        (strlen (cv->microphone_device) > MAX_POLICY_ENTRY_BYTES ||
         contains_ascii_control (cv->microphone_device) ||
         !g_utf8_validate (cv->microphone_device, -1, NULL))) {
        g_set_error (error, config_error_quark (), 1,
                     "microphone_device is oversized or invalid UTF-8");
        goto fail;
    }

    cv->allow_list = get_optional_list (kf, "allow_list", error);
    if (error && *error) goto fail;
    cv->deny_list = get_optional_list (kf, "deny_list", error);
    if (error && *error) goto fail;
    cv->mic_allow_list = get_optional_list (kf, "mic_allow_list", error);
    if (error && *error) goto fail;
    cv->mic_deny_list = get_optional_list (kf, "mic_deny_list", error);
    if (error && *error) goto fail;

    g_key_file_unref (kf);
    g_free (path);
    return cv;

fail:
    config_values_free (cv);
    g_key_file_unref (kf);
    g_free (path);
    return NULL;
}
