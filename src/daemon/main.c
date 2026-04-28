#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include "sg-notification.h"
#include "sg-dbus.h"
#include "sg-state.h"
#include "sg-logging.h"
#include "main.h"
#include "version.h"

typedef struct {
    GMainLoop    *loop;
    ConfigValues *cfg;
    gchar        *config_path; /* may be NULL: use default */
    guint         poll_source_id;
} Ctx;

static Ctx app_ctx = { 0 };

/* ---------- helpers ---------- */

static gboolean
strv_contains (gchar **list, const gchar *name)
{
    if (!list || !name) return FALSE;
    for (gchar **p = list; *p; ++p) {
        if (g_strcmp0 (*p, name) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean
should_notify (const gchar *proc, gchar **allow, gchar **deny)
{
    /* Unknown proc: always notify. */
    if (!proc || !*proc) return TRUE;
    if (strv_contains (deny, proc))  return TRUE;
    if (strv_contains (allow, proc)) return FALSE;
    return TRUE;
}

static void
publish_status_to_dbus (void)
{
    SGStatus s = {
        sg_state.webcam_active,
        sg_state.mic_active,
        sg_state.webcam_proc,
        sg_state.mic_proc,
    };
    sg_dbus_update_status (&s);
}

static void
notify_webcam (const gchar *proc, gint timeout_s)
{
    gchar *body = proc && *proc
        ? g_strdup_printf ("%s is currently using your webcam", proc)
        : g_strdup ("A process is currently using your webcam");
    sg_send_notification ("Webcam in use",
                          body,
                          "camera-web",
                          "device",
                          timeout_s * 1000,
                          SG_NOTIFY_CRITICAL);
    g_free (body);
}

static void
notify_mic (const gchar *proc, gint timeout_s)
{
    gchar *body = proc && *proc
        ? g_strdup_printf ("%s is currently using your microphone", proc)
        : g_strdup ("A process is currently using your microphone");
    sg_send_notification ("Microphone in use",
                          body,
                          "audio-input-microphone",
                          "device",
                          timeout_s * 1000,
                          SG_NOTIFY_CRITICAL);
    g_free (body);
}

/* ---------- webcam periodic poll ---------- */

static gboolean
on_periodic_check (gpointer user_data)
{
    Ctx *ctx = user_data;
    struct _devs *head = list_webcam ();
    gboolean any_active = FALSE;
    gchar *active_proc = NULL;

    while (head) {
        gchar *proc = NULL;
        if (check_webcam (head->dev_name, &proc)) {
            any_active = TRUE;
            if (!active_proc) {
                active_proc = proc;
            } else {
                g_free (proc);
            }
        } else {
            g_free (proc);
        }
        struct _devs *next = head->next;
        g_free (head->dev_name);
        g_free (head);
        head = next;
    }

    gboolean changed = sg_state_set_webcam (any_active, active_proc);
    if (changed && any_active &&
        should_notify (active_proc, ctx->cfg->allow_list, ctx->cfg->deny_list)) {
        notify_webcam (active_proc, ctx->cfg->notification_timeout);
    }
    if (changed) publish_status_to_dbus ();
    g_free (active_proc);
    return G_SOURCE_CONTINUE;
}

/* ---------- mic event callback ---------- */

static void
on_mic_state_changed (gboolean active, const gchar *proc, gpointer user_data)
{
    Ctx *ctx = user_data;
    gboolean changed = sg_state_set_mic (active, proc);
    if (changed && active &&
        should_notify (proc, ctx->cfg->mic_allow_list, ctx->cfg->mic_deny_list)) {
        notify_mic (proc, ctx->cfg->notification_timeout);
    }
    if (changed) publish_status_to_dbus ();
}

/* ---------- config (re)load ---------- */

static void
apply_config (Ctx *ctx, ConfigValues *new_cfg)
{
    gboolean first_init  = (ctx->poll_source_id == 0);
    guint64  old_interval = ctx->cfg ? ctx->cfg->check_interval : 0;
    gchar   *old_filter   = ctx->cfg && ctx->cfg->microphone_device
                           ? g_strdup (ctx->cfg->microphone_device) : NULL;

    if (ctx->cfg) config_values_free (ctx->cfg);
    ctx->cfg = new_cfg;

    if (first_init || old_interval != ctx->cfg->check_interval) {
        if (ctx->poll_source_id) {
            g_source_remove (ctx->poll_source_id);
            ctx->poll_source_id = 0;
        }
        ctx->poll_source_id = g_timeout_add_seconds (
            (guint) ctx->cfg->check_interval, on_periodic_check, ctx);
    }

    /* (Re)init the mic monitor on first call or when the filter changed. */
    const gchar *new_filter = ctx->cfg->microphone_device;
    if (first_init || g_strcmp0 (old_filter, new_filter) != 0) {
        mic_monitor_uninit ();
        if (!mic_monitor_init (new_filter, on_mic_state_changed, ctx)) {
            g_message ("Mic monitor not available (PipeWire not reachable)");
        }
    }
    g_free (old_filter);
}

static void
reload_config_now (gpointer user_data)
{
    Ctx *ctx = user_data;
    g_message ("Reloading config");
    ConfigValues *new_cfg = load_config_file (ctx->config_path);
    apply_config (ctx, new_cfg);
}

static gboolean
on_sighup (gpointer user_data)
{
    reload_config_now (user_data);
    return G_SOURCE_CONTINUE;
}

/* ---------- shutdown ---------- */

static gboolean
on_term_signal (gpointer user_data)
{
    Ctx *ctx = user_data;
    g_message ("Shutdown signal received");
    if (ctx->loop) g_main_loop_quit (ctx->loop);
    return G_SOURCE_REMOVE;
}

/* ---------- argv ---------- */

static void
print_usage (FILE *out, const gchar *prog)
{
    fprintf (out,
             "Usage: %s [options]\n"
             "  -h, --help               Show this help\n"
             "  -v, --version            Show version\n"
             "  -c, --config <path>      Use the given config file\n",
             prog);
}

int
main (int argc, char **argv)
{
    const gchar *config_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0 (argv[i], "-v") == 0 || g_strcmp0 (argv[i], "--version") == 0) {
            g_print ("%s v%s developed by %s\n",
                     SW_NAME, SNOOPGUARD_VERSION_FULL, DEV_NAME);
            return 0;
        }
        if (g_strcmp0 (argv[i], "-h") == 0 || g_strcmp0 (argv[i], "--help") == 0) {
            print_usage (stdout, argv[0]);
            return 0;
        }
        if ((g_strcmp0 (argv[i], "-c") == 0 || g_strcmp0 (argv[i], "--config") == 0) &&
            i + 1 < argc) {
            config_override = argv[++i];
            continue;
        }
        if (g_str_has_prefix (argv[i], "--config=")) {
            config_override = argv[i] + strlen ("--config=");
            continue;
        }
        fprintf (stderr, "Unknown argument: %s\n", argv[i]);
        print_usage (stderr, argv[0]);
        return 2;
    }

    /* Logging needs to be available before everything else. */
    gchar *state_dir = g_build_filename (g_get_user_state_dir (), "snoop-guard", NULL);
    g_mkdir_with_parents (state_dir, 0700);
    gchar *event_log = g_build_filename (state_dir, "events.log", NULL);

    app_ctx.config_path = config_override ? g_strdup (config_override) : NULL;
    ConfigValues *cfg = load_config_file (app_ctx.config_path);

    sg_log_init (event_log, cfg->log_max_bytes);
    g_free (event_log);
    g_free (state_dir);

    sg_state_init ();

    app_ctx.loop = g_main_loop_new (NULL, FALSE);

    sg_dbus_init (reload_config_now, &app_ctx);

    /* Acquire a session bus connection for notifications. We do this
     * directly (not via sg_dbus_get_bus) since g_bus_own_name is async. */
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        g_warning ("Couldn't open session bus: %s", err ? err->message : "(unknown)");
        if (err) g_clear_error (&err);
    } else {
        sg_notification_init (bus, "snoop-guard");
    }

    /* Now safe to install the timer + mic monitor based on cfg. */
    apply_config (&app_ctx, cfg);

    g_unix_signal_add (SIGHUP,  on_sighup,      &app_ctx);
    g_unix_signal_add (SIGINT,  on_term_signal, &app_ctx);
    g_unix_signal_add (SIGTERM, on_term_signal, &app_ctx);

    g_message ("Starting %s with check interval %lus",
               SW_NAME, (unsigned long) app_ctx.cfg->check_interval);

    g_main_loop_run (app_ctx.loop);

    /* ---------- shutdown / cleanup ---------- */
    if (app_ctx.poll_source_id) g_source_remove (app_ctx.poll_source_id);
    mic_monitor_uninit ();
    sg_dbus_uninit ();
    sg_notification_uninit ();
    if (bus) g_object_unref (bus);
    config_values_free (app_ctx.cfg);
    g_free (app_ctx.config_path);
    sg_state_cleanup ();
    g_main_loop_unref (app_ctx.loop);
    sg_log_uninit ();
    return 0;
}
