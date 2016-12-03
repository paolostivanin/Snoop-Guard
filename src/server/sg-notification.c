#include <glib.h>
#include <libnotify/notify.h>
#include "sg-notification.h"


gint
sg_notification_init (const gchar *app_name)
{
    if (!notify_is_initted ()) {
        if (!notify_init (app_name)) {
            return INIT_ERROR;
        }
        return INIT_OK;
    }
    return ALREADY_INITTED;
}


void
sg_notification_uninit ()
{
    notify_uninit ();
}


NotifyNotification *
sg_create_notification (const gchar *summary, const gchar *body)
{
    return notify_notification_new (summary, body, NULL);
}


void
sg_notification_update (NotifyNotification *n, const gchar *summary, const gchar* body)
{
    notify_notification_update (n, summary, body, NULL);
}


void
sg_notification_show (NotifyNotification *n, gint timeout)
{
    GError *err = NULL;

    if (timeout > 0) {
        notify_notification_set_timeout (n, timeout);
    }

    notify_notification_show (n, &err);
    if (err != NULL) {
        g_printerr ("%s\n", err->message);
        g_clear_error (&err);
    }
}