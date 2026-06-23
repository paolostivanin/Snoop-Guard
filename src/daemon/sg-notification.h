#pragma once

#include <gio/gio.h>

typedef enum {
    INIT_ERROR = -1,
    INIT_OK = 0,
    ALREADY_INITTED = 1,
} SGNotifyInitResult;

typedef enum {
    SG_NOTIFY_NORMAL = 0,
    SG_NOTIFY_CRITICAL = 1,
} SGNotifyUrgency;

/* Initialize notifications. The given GDBusConnection is borrowed (not owned)
 * and must outlive any sg_send_notification call. */
gint sg_notification_init (GDBusConnection *bus, const gchar *app_name);

void sg_notification_uninit (void);

/* Send a notification via org.freedesktop.Notifications. Asynchronous; never
 * blocks the caller. body and summary are escaped for Pango markup. icon may
 * be NULL or a freedesktop icon name (e.g. "camera-web"). */
void sg_send_notification (const gchar *summary,
                           const gchar *body,
                           const gchar *icon,
                           const gchar *category,
                           gint timeout_ms,
                           SGNotifyUrgency urgency);
