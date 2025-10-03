#include <glib.h>
#include "sg-notification.h"

static gchar *app_name_cached = NULL;

gint sg_notification_init (const gchar *app_name)
{
    if (app_name_cached) return ALREADY_INITTED;
    app_name_cached = g_strdup(app_name ? app_name : "SnoopGuard");
    return INIT_OK;
}

void sg_notification_uninit (void)
{
    if (app_name_cached) { g_free(app_name_cached); app_name_cached = NULL; }
}

static void send_notify_call(const gchar *summary, const gchar *body, gint timeout_ms)
{
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) { g_clear_error(&err); return; }

    // org.freedesktop.Notifications Notify method signature:
    // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout) -> uint32
    const gchar *app_icon = "";
    guint32 replaces_id = 0;
    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));

    GVariant *ret = g_dbus_connection_call_sync(
            conn,
            "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
            "Notify",
            g_variant_new("(susssasa{sv}i)",
                          app_name_cached ? app_name_cached : "SnoopGuard",
                          replaces_id,
                          app_icon,
                          summary ? summary : "",
                          body ? body : "",
                          &actions,
                          &hints,
                          timeout_ms),
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &err);
    if (ret) g_variant_unref(ret);
    if (err) { g_clear_error(&err); }
    g_object_unref(conn);
}

void sg_send_notification (const gchar *summary, const gchar *body, gint timeout_ms)
{
    send_notify_call(summary, body, timeout_ms);
}