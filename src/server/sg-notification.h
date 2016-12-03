#pragma once

#define NOTIFICATION_ERROR -2;
#define INIT_ERROR -1;
#define INIT_OK 0;
#define ALREADY_INITTED 1;

gint sg_notification_init (const gchar *app_name);

void sg_notification_uninit ();

NotifyNotification *sg_create_notification (const gchar *summary, const gchar *body);

void sg_notification_show (NotifyNotification *n, gint timeout);