#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include "sg-notification.h"
#include "sg-dbus.h"
#include "sg-state.h"
#include "sg-logging.h"
#include "sg-policy.h"
#include "main.h"
#include "version.h"

typedef struct {
    GMainLoop    *loop;
    ConfigValues *cfg;
    gchar        *config_path; /* may be NULL: use default */
    guint         poll_source_id;
    guint         mic_retry_source_id;
    guint         mic_retry_seconds;
    GHashTable   *webcam_notified;
    GHashTable   *mic_notified;
    gboolean      webcam_health_notified;
    gboolean      mic_health_notified;
} Ctx;

static Ctx app_ctx = { 0 };

/* ---------- helpers ---------- */

static void
replace_notified_set (GHashTable **set,
                      gchar **processes,
                      gchar **allow,
                      gchar **deny,
                      void (*notify_fn) (const gchar *, gint),
                      gint timeout_s,
                      gboolean active)
{
    GHashTable *next = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; processes && processes[i]; i++) {
        const gchar *proc = processes[i];
        if (!sg_policy_should_notify (proc, allow, deny)) continue;
        if (!g_hash_table_contains (*set, proc)) notify_fn (proc, timeout_s);
        g_hash_table_add (next, g_strdup (proc));
    }
    if (active && (!processes || !processes[0])) {
        const gchar *unknown = "<unknown>";
        if (!g_hash_table_contains (*set, unknown)) notify_fn (NULL, timeout_s);
        g_hash_table_add (next, g_strdup (unknown));
    }
    g_hash_table_destroy (*set);
    *set = next;
}

static void
publish_status_to_dbus (void)
{
    SGStatus s = {
        .webcam_active = sg_state.webcam_active,
        .mic_active = sg_state.mic_active,
        .webcam_proc = sg_state.webcam_proc,
        .mic_proc = sg_state.mic_proc,
        .webcam_processes = sg_state.webcam_processes,
        .mic_processes = sg_state.mic_processes,
        .webcam_unknown_devices = sg_state.webcam_unknown_devices,
        .webcam_health = sg_state.webcam_health,
        .mic_health = sg_state.mic_health,
        .webcam_diagnostic = sg_state.webcam_diagnostic,
        .mic_diagnostic = sg_state.mic_diagnostic,
    };
    sg_dbus_update_status (&s);
}

static void
notify_health (const gchar *monitor, const gchar *diagnostic)
{
    gchar *body = g_strdup_printf (
        "%s monitoring is degraded: %s",
        monitor, diagnostic && *diagnostic ? diagnostic : "unknown error");
    sg_send_notification ("Snoop Guard monitoring degraded", body,
                          "dialog-warning", "device", 0, SG_NOTIFY_CRITICAL);
    g_free (body);
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
    SGMonitorSnapshot snapshot = { 0 };
    check_webcams (&snapshot);
    gboolean changed = sg_state_set_webcam (
        snapshot.active, snapshot.processes, snapshot.unknown_devices,
        sg_monitor_health_to_string (snapshot.health), snapshot.diagnostic);
    replace_notified_set (&ctx->webcam_notified, snapshot.processes,
                          ctx->cfg->allow_list, ctx->cfg->deny_list,
                          notify_webcam, ctx->cfg->notification_timeout,
                          snapshot.active);
    if (snapshot.health == SG_MONITOR_OK) {
        ctx->webcam_health_notified = FALSE;
    } else if (!ctx->webcam_health_notified) {
        notify_health ("Webcam", snapshot.diagnostic);
        ctx->webcam_health_notified = TRUE;
    }
    if (changed) publish_status_to_dbus ();
    sg_monitor_snapshot_clear (&snapshot);
    return G_SOURCE_CONTINUE;
}

/* ---------- mic event callback ---------- */

static gboolean retry_mic_monitor (gpointer user_data);

static void
schedule_mic_retry (Ctx *ctx)
{
    if (ctx->mic_retry_source_id) return;
    if (ctx->mic_retry_seconds == 0) ctx->mic_retry_seconds = 1;
    ctx->mic_retry_source_id = g_timeout_add_seconds (
        ctx->mic_retry_seconds, retry_mic_monitor, ctx);
    ctx->mic_retry_seconds = MIN (ctx->mic_retry_seconds * 2, 60U);
}

static void
on_mic_state_changed (const SGMonitorSnapshot *snapshot, gpointer user_data)
{
    Ctx *ctx = user_data;
    gboolean changed = sg_state_set_mic (
        snapshot->active, snapshot->processes,
        sg_monitor_health_to_string (snapshot->health), snapshot->diagnostic);
    replace_notified_set (&ctx->mic_notified, snapshot->processes,
                          ctx->cfg->mic_allow_list, ctx->cfg->mic_deny_list,
                          notify_mic, ctx->cfg->notification_timeout,
                          snapshot->active);
    if (snapshot->health == SG_MONITOR_OK) {
        if (ctx->mic_retry_source_id) {
            g_source_remove (ctx->mic_retry_source_id);
            ctx->mic_retry_source_id = 0;
        }
        ctx->mic_health_notified = FALSE;
        ctx->mic_retry_seconds = 1;
    } else {
        if (!ctx->mic_health_notified) {
            notify_health ("Microphone", snapshot->diagnostic);
            ctx->mic_health_notified = TRUE;
        }
        schedule_mic_retry (ctx);
    }
    if (changed) publish_status_to_dbus ();
}

static gboolean
retry_mic_monitor (gpointer user_data)
{
    Ctx *ctx = user_data;
    ctx->mic_retry_source_id = 0;
    mic_monitor_uninit ();
    if (!mic_monitor_init (ctx->cfg->microphone_device,
                           on_mic_state_changed, ctx)) {
        SGMonitorSnapshot snapshot = {
            .health = SG_MONITOR_UNAVAILABLE,
            .diagnostic = g_strdup ("PipeWire is not reachable; retrying"),
        };
        on_mic_state_changed (&snapshot, ctx);
        sg_monitor_snapshot_clear (&snapshot);
    }
    return G_SOURCE_REMOVE;
}

/* ---------- config (re)load ---------- */

static void
apply_config (Ctx *ctx, ConfigValues *new_cfg)
{
    g_return_if_fail (ctx != NULL);
    g_return_if_fail (new_cfg != NULL);
    gboolean first_init  = (ctx->poll_source_id == 0);
    guint64  old_interval = ctx->cfg ? ctx->cfg->check_interval : 0;
    gchar   *old_filter   = ctx->cfg && ctx->cfg->microphone_device
                           ? g_strdup (ctx->cfg->microphone_device) : NULL;

    if (ctx->cfg) config_values_free (ctx->cfg);
    ctx->cfg = new_cfg;
    sg_log_set_max_bytes (new_cfg->log_max_bytes);
    replace_notified_set (&ctx->webcam_notified, sg_state.webcam_processes,
                          new_cfg->allow_list, new_cfg->deny_list,
                          notify_webcam, new_cfg->notification_timeout,
                          sg_state.webcam_active);
    replace_notified_set (&ctx->mic_notified, sg_state.mic_processes,
                          new_cfg->mic_allow_list, new_cfg->mic_deny_list,
                          notify_mic, new_cfg->notification_timeout,
                          sg_state.mic_active);

    if (first_init || old_interval != new_cfg->check_interval) {
        if (ctx->poll_source_id) {
            g_source_remove (ctx->poll_source_id);
            ctx->poll_source_id = 0;
        }
        ctx->poll_source_id = g_timeout_add_seconds (
            (guint) new_cfg->check_interval, on_periodic_check, ctx);
        on_periodic_check (ctx);
    }

    /* (Re)init the mic monitor on first call or when the filter changed. */
    const gchar *new_filter = new_cfg->microphone_device;
    if (first_init || g_strcmp0 (old_filter, new_filter) != 0) {
        mic_monitor_uninit ();
        if (!mic_monitor_init (new_filter, on_mic_state_changed, ctx)) {
            SGMonitorSnapshot snapshot = {
                .health = SG_MONITOR_UNAVAILABLE,
                .diagnostic = g_strdup ("PipeWire is not reachable; retrying"),
            };
            on_mic_state_changed (&snapshot, ctx);
            sg_monitor_snapshot_clear (&snapshot);
        }
    }
    g_free (old_filter);
}

static gboolean
reload_config_now (gpointer user_data, GError **error)
{
    Ctx *ctx = user_data;
    g_message ("Reloading config");
    ConfigValues *new_cfg = load_config_file (
        ctx->config_path, ctx->config_path != NULL, error);
    if (!new_cfg) return FALSE;
    apply_config (ctx, new_cfg);
    return TRUE;
}

static gboolean
on_sighup (gpointer user_data)
{
    GError *error = NULL;
    if (!reload_config_now (user_data, &error)) {
        g_warning ("Config reload failed; retaining previous config: %s",
                   error ? error->message : "unknown error");
        g_clear_error (&error);
    }
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
    gchar *event_log = g_build_filename (state_dir, "events.log", NULL);

    app_ctx.config_path = config_override ? g_strdup (config_override) : NULL;
    GError *error = NULL;
    ConfigValues *cfg = load_config_file (
        app_ctx.config_path, app_ctx.config_path != NULL, &error);
    if (!cfg) {
        g_printerr ("Configuration error: %s\n",
                    error ? error->message : "unknown error");
        g_clear_error (&error);
        g_free (state_dir);
        g_free (event_log);
        g_free (app_ctx.config_path);
        return 1;
    }

    if (!sg_log_init (event_log, cfg->log_max_bytes, &error)) {
        g_printerr ("Logging initialization failed: %s\n",
                    error ? error->message : "unknown error");
        g_clear_error (&error);
        config_values_free (cfg);
        g_free (event_log);
        g_free (state_dir);
        g_free (app_ctx.config_path);
        return 1;
    }
    g_free (event_log);
    g_free (state_dir);

    sg_state_init ();
    app_ctx.webcam_notified = g_hash_table_new_full (
        g_str_hash, g_str_equal, g_free, NULL);
    app_ctx.mic_notified = g_hash_table_new_full (
        g_str_hash, g_str_equal, g_free, NULL);
    app_ctx.mic_retry_seconds = 1;

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
    gboolean fatal_bus_error = sg_dbus_had_fatal_error ();
    if (app_ctx.poll_source_id) g_source_remove (app_ctx.poll_source_id);
    if (app_ctx.mic_retry_source_id) g_source_remove (app_ctx.mic_retry_source_id);
    mic_monitor_uninit ();
    sg_dbus_uninit ();
    sg_notification_uninit ();
    if (bus) g_object_unref (bus);
    config_values_free (app_ctx.cfg);
    g_hash_table_destroy (app_ctx.webcam_notified);
    g_hash_table_destroy (app_ctx.mic_notified);
    g_free (app_ctx.config_path);
    sg_state_cleanup ();
    g_main_loop_unref (app_ctx.loop);
    sg_log_uninit ();
    return fatal_bus_error ? 1 : 0;
}
