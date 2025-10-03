#include <glib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "sg-notification.h"
#include "sg-dbus.h"
#include "sg-state.h"
#include "main.h"
#include "../common.h"

static GMainLoop *main_loop = NULL;

static gboolean on_periodic_check(gpointer user_data);

static void signal_handler (gint sig, siginfo_t *si, gpointer context);

int main (int argc, char **argv)
{
    if (argc > 1 && (g_strcmp0 (argv[1], "-v") == 0 || g_strcmp0 (argv[1], "--version") == 0)) {
        g_print ("%s v%s developed by %s <%s>\n", SW_NAME, SW_VERSION, DEV_NAME, DEV_EMAIL);
        return 0;
    }

    struct sigaction sig_sa;
    memset (&sig_sa, 0, sizeof (sig_sa));
    sig_sa.sa_flags = SA_SIGINFO;
    sig_sa.sa_sigaction = signal_handler;
    sigaction (SIGTERM, &sig_sa, NULL);
    sigaction (SIGINT, &sig_sa, NULL);
    sigaction (SIGALRM, &sig_sa, NULL);

    gint notification_server_status = sg_notification_init ("sg-server");
    if (notification_server_status == INIT_ERROR)
        g_printerr ("Couldn't initialize notification server, only systemd journal will be used\n");

    ConfigValues *cfg_values = load_config_file ();

    if (cfg_values->microphone_device && g_strcmp0 (cfg_values->microphone_device, "sysdefault") == 0) {
        gint status = check_sysdefault_dev ();
        if (status != SYSDEFAULT_FOUND) {
            g_printerr ("Couldn't find sysdefault device\n");
            g_free (cfg_values);
            sg_notification_uninit ();
            return -1;
        }
    }

    sg_state_init();

    main_loop = g_main_loop_new(NULL, FALSE);
    sg_dbus_init(main_loop);

    // Store cfg and notif status in a struct for periodic handler
    typedef struct { ConfigValues *cfg; gint nss; } Ctx;
    Ctx *ctx = g_new0(Ctx, 1); ctx->cfg = cfg_values; ctx->nss = notification_server_status;

    g_timeout_add_seconds((guint)cfg_values->check_interval, on_periodic_check, ctx);

    g_print ("Starting %s with a checking interval of %lu seconds...\n", SW_NAME, (unsigned long)cfg_values->check_interval);
    g_main_loop_run(main_loop);

    // cleanup
    if (cfg_values->microphone_device != NULL)
        g_free (cfg_values->microphone_device);
    if (cfg_values->allow_list != NULL)
        g_strfreev (cfg_values->allow_list);
    if (cfg_values->deny_list != NULL)
        g_strfreev (cfg_values->deny_list);
    g_free (cfg_values);
    sg_notification_uninit ();
    g_main_loop_unref(main_loop);

    return 0;
}

static gboolean on_periodic_check(gpointer user_data)
{
    typedef struct { ConfigValues *cfg; gint nss; } Ctx;
    Ctx *ctx = (Ctx*)user_data;

    // webcam check
    struct _devs *head = list_webcam (); struct _devs *tmp;
    gboolean any_webcam_active = FALSE;
    while (head) {
        check_webcam (ctx->nss, head->dev_name, ctx->cfg->allow_list, ctx->cfg->deny_list);
        if (sg_state.webcam_active) any_webcam_active = TRUE;
        g_free (head->dev_name);
        tmp = head; head = head->next; g_free (tmp);
    }
    if (!any_webcam_active) sg_state_set_webcam(FALSE, NULL);

    // mic check
    if (ctx->cfg->microphone_device != NULL) {
        gint ms = get_mic_status (ctx->cfg->microphone_device);
        sg_state_set_mic(ms == MIC_ALREADY_IN_USE, NULL);
    }

    SGStatus s = { sg_state.webcam_active, sg_state.mic_active, sg_state.webcam_proc, sg_state.mic_proc };
    sg_dbus_update_status(&s);
    return TRUE; // keep the timeout
}

static void
signal_handler (gint sig, siginfo_t *si __attribute__((unused)), gpointer context  __attribute__((unused)))
{
    switch (sig) {
        case SIGINT:
        case SIGTERM:
        case SIGALRM:
            g_print ("Cleaning up and exiting...\n");
            if (main_loop) g_main_loop_quit(main_loop);
            break;
        default:
            break;
    }
}