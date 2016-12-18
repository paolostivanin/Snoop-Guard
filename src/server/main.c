#include <glib.h>
#include <signal.h>
#include "sg-notification.h"
#include "main.h"


volatile static gboolean sigterm = FALSE;

static void on_sigterm (gint);


gint
main (gint argc, gchar **argv)
{
    // TODO: get which app is using the webcam
    signal (SIGTERM, on_sigterm);

    struct _devs *head, *tmp;

    if (sg_notification_init ("sg-server") == INIT_ERROR) {
        g_printerr ("Couldn't initialize notification server, only systemd journal will be used\n");
    }

    ConfigValues *cfg_values = load_config_file ();

    if (g_strcmp0 (cfg_values->microphone_device, "sysdefault") == 0) {
        gint status = check_sysdefault_dev ();
        if (status != SYSDEFAULT_FOUND) {
            g_printerr("Couldn't find sysdefault device\n");
            g_free (cfg_values);
            sg_notification_uninit ();
            return -1;
        }
    }

    g_print ("Starting %s with a checking interval of %ld seconds...\n", SW_NAME, cfg_values->check_interval);
    while (!sigterm) {
        head = list_webcam ();
        while (head) {
            check_webcam (head->dev_name, cfg_values->ignore_apps);
            g_free (head->dev_name);
            tmp = head;
            head = head->next;
            g_free (tmp);
        }

        if (cfg_values->microphone_device != NULL) {
            get_mic_status (cfg_values->microphone_device);
        }

        g_usleep (G_USEC_PER_SEC * cfg_values->check_interval);
    }

    if (cfg_values->microphone_device != NULL) {
        g_free (cfg_values->microphone_device);
    }
    if (cfg_values->ignore_apps != NULL) {
        g_strfreev (cfg_values->ignore_apps);
    }

    g_free (cfg_values);
    sg_notification_uninit ();

    return 0;
}


static void
on_sigterm (gint sig_num)
{
    sigterm = TRUE;
}