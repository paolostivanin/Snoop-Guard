#include <glib.h>
#include "sg-notification.h"

static gchar *app_name_cached = NULL;
static GDBusConnection *bus_conn = NULL; /* borrowed */

gint
sg_notification_init (GDBusConnection *bus, const gchar *app_name)
{
    if (app_name_cached) return ALREADY_INITTED;
    if (!bus) return INIT_ERROR;
    bus_conn = bus;
    app_name_cached = g_strdup (app_name ? app_name : "SnoopGuard");
    return INIT_OK;
}

void
sg_notification_uninit (void)
{
    g_clear_pointer (&app_name_cached, g_free);
    bus_conn = NULL;
}

static void
on_notify_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void) user_data;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &err);
    if (ret) g_variant_unref (ret);
    if (err) {
        g_warning ("Notification delivery failed: %s", err->message);
        g_clear_error (&err);
    }
}

void
sg_send_notification (const gchar *summary,
                      const gchar *body,
                      const gchar *icon,
                      const gchar *category,
                      gint timeout_ms,
                      SGNotifyUrgency urgency)
{
    if (!bus_conn || !app_name_cached) return;

    /* org.freedesktop.Notifications Notify signature:
     * (susssasa{sv}i) -> u
     */
    GVariantBuilder actions;
    g_variant_builder_init (&actions, G_VARIANT_TYPE ("as"));

    GVariantBuilder hints;
    g_variant_builder_init (&hints, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&hints, "{sv}", "urgency",
                           g_variant_new_byte (urgency == SG_NOTIFY_CRITICAL ? 2 : 1));
    if (category && *category) {
        g_variant_builder_add (&hints, "{sv}", "category",
                               g_variant_new_string (category));
    }
    g_variant_builder_add (&hints, "{sv}", "desktop-entry",
                           g_variant_new_string ("snoop-guard"));

    /* Escape user-controlled strings (process names) for any notification
     * server that interprets Pango markup. */
    gchar *summary_safe = g_markup_escape_text (summary ? summary : "", -1);
    gchar *body_safe    = g_markup_escape_text (body ? body : "", -1);

    GVariant *params = g_variant_new ("(susssasa{sv}i)",
                                      app_name_cached,
                                      (guint32) 0,
                                      icon ? icon : "",
                                      summary_safe,
                                      body_safe,
                                      &actions,
                                      &hints,
                                      timeout_ms);
    g_dbus_connection_call (bus_conn,
                            "org.freedesktop.Notifications",
                            "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications",
                            "Notify",
                            params,
                            G_VARIANT_TYPE ("(u)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            on_notify_done,
                            NULL);

    g_free (summary_safe);
    g_free (body_safe);
}
