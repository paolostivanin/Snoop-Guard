#include <glib.h>
#include <sys/types.h>
#include <signal.h>
#include "sg-notification.h"
#include "main.h"


volatile static gboolean signal_received = FALSE;

static void signal_handler (gint sig, siginfo_t *si, gpointer context);


gint
main (gint argc, gchar **argv)
{
    if (g_strcmp0 (argv[1], "-v") == 0 || g_strcmp0 (argv[1], "--version") == 0) {
        g_print ("%s v%s developed by %s <%s>", SW_NAME, SW_VERSION, DEV_NAME, DEV_EMAIL);
        return 0;
    }

    struct sigaction sig_sa;
    sig_sa.sa_flags = SA_SIGINFO;
    sig_sa.sa_sigaction = signal_handler;
    sigaction (SIGTERM, &sig_sa, NULL);
    sigaction (SIGINT, &sig_sa, NULL);
    sigaction (SIGALRM, &sig_sa, NULL);

    struct _devs *head, *tmp;

    gint notification_server_status = sg_notification_init ("sg-server");
    if (notification_server_status == INIT_ERROR)
        g_printerr ("Couldn't initialize notification server, only systemd journal will be used\n");

    ConfigValues *cfg_values = load_config_file ();

    if (g_strcmp0 (cfg_values->microphone_device, "sysdefault") == 0) {
        gint status = check_sysdefault_dev ();
        if (status != SYSDEFAULT_FOUND) {
            g_printerr ("Couldn't find sysdefault device\n");
            g_free (cfg_values);
            sg_notification_uninit ();
            return -1;
        }
    }

    g_print ("Starting %s with a checking interval of %ld seconds...\n", SW_NAME, cfg_values->check_interval);
    while (!signal_received) {
        head = list_webcam ();
        while (head) {
            check_webcam (notification_server_status, head->dev_name, cfg_values->ignore_apps);
            g_free (head->dev_name);
            tmp = head;
            head = head->next;
            g_free (tmp);
        }

        if (cfg_values->microphone_device != NULL)
            get_mic_status (cfg_values->microphone_device);

        g_usleep (G_USEC_PER_SEC * cfg_values->check_interval);
    }

    if (cfg_values->microphone_device != NULL)
        g_free (cfg_values->microphone_device);

    if (cfg_values->ignore_apps != NULL)
        g_strfreev (cfg_values->ignore_apps);

    g_free (cfg_values);
    sg_notification_uninit ();

    return 0;
}


static void
signal_handler (gint sig, siginfo_t *si __attribute__((unused)), gpointer context  __attribute__((unused)))
{
    switch (sig) {
        case SIGINT:
        case SIGTERM:
        case SIGALRM:
            g_print ("Cleaning up and exiting...\n");
            signal_received = TRUE;
            break;
        default:
            break;
    }
}