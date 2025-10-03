#pragma once

#include <gio/gio.h>

#define INIT_ERROR -1
#define INIT_OK 0
#define ALREADY_INITTED 1

// Initialize notifications (no-op for D-Bus based implementation)
gint sg_notification_init (const gchar *app_name);

void sg_notification_uninit (void);

// Send a notification via org.freedesktop.Notifications
void sg_send_notification (const gchar *summary, const gchar *body, gint timeout_ms);