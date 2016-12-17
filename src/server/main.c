#include <glib.h>
#include "sg-notification.h"
#include "sg-logging.h"
#include "main.h"


gint
main (gint argc, gchar **argv)
{
    /* TODO:
     *  - write log to proper dir
     *  - write proper systemd daemon
     *  - sleep default or as specified by interval
     */
    struct _devs *head, *tmp;

    ConfigValues *cfg_values = load_config_file ();

    if (sg_notification_init ("sg-server") == INIT_ERROR) {
        g_printerr ("Couldn't initialize notification server, only logs will be used\n");
    }

    head = list_webcam ();
    while (head) {
        check_webcam (head->dev_name, cfg_values->ignore_apps);
        g_free (head->dev_name);
        tmp = head;
        head = head->next;
        g_free (tmp);
    }

    if (cfg_values->microphone_device != NULL) {
        if (get_mic_status (cfg_values->microphone_device) == MIC_ALREADY_IN_USE) {
            g_printerr ("Your mic IS being used!\n");
        } else {
            g_printerr ("The mic is NOT being used\n");
        }
        g_free (cfg_values->microphone_device);
    }

    if (cfg_values->ignore_apps != NULL) {
        g_strfreev (cfg_values->ignore_apps);
    }

    g_free (cfg_values);
    sg_notification_uninit ();

    return 0;
}
