#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <string.h>
#include "version.h"
#include "sg-json.h"

#define SG_BUS    "org.snoopguard.Service"
#define SG_PATH   "/org/snoopguard/Service"
#define SG_IFACE  "org.snoopguard.Service"

static gboolean opt_json = FALSE;

/* ---------- helpers ---------- */

static GDBusConnection *
connect_session (void)
{
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) {
        g_printerr ("D-Bus session connect failed: %s\n",
                    err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        return NULL;
    }
    return conn;
}

static void
print_status_pair (gboolean w, gboolean m, const gchar *wp, const gchar *mp,
                   const gchar *ts)
{
    if (opt_json) {
        gchar *jw = sg_json_escape (wp);
        gchar *jm = sg_json_escape (mp);
        if (ts) {
            printf ("{\"timestamp\":\"%s\",\"webcam_active\":%s,"
                    "\"webcam_proc\":\"%s\",\"mic_active\":%s,"
                    "\"mic_proc\":\"%s\"}\n",
                    ts, w ? "true" : "false", jw,
                    m ? "true" : "false", jm);
        } else {
            printf ("{\"webcam_active\":%s,\"webcam_proc\":\"%s\","
                    "\"mic_active\":%s,\"mic_proc\":\"%s\"}\n",
                    w ? "true" : "false", jw,
                    m ? "true" : "false", jm);
        }
        g_free (jw); g_free (jm);
        return;
    }
    if (ts) printf ("[%s] ", ts);
    printf ("webcam_active: %s", w ? "true" : "false");
    if (wp && *wp) printf (" (proc: %s)", wp);
    printf ("\n");
    if (ts) printf ("[%s] ", ts);
    printf ("mic_active: %s", m ? "true" : "false");
    if (mp && *mp) printf (" (proc: %s)", mp);
    printf ("\n");
}

static gboolean
print_status_on_connection (GDBusConnection *conn)
{
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "GetStatus",
        NULL, G_VARIANT_TYPE ("(bbss)"),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (!ret) {
        g_printerr ("Call failed (is sg-daemon running?): %s\n",
                    err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        return FALSE;
    }
    gboolean w, m; const gchar *wp; const gchar *mp;
    g_variant_get (ret, "(bb&s&s)", &w, &m, &wp, &mp);
    print_status_pair (w, m, wp, mp, NULL);
    g_variant_unref (ret);
    return TRUE;
}

static gboolean
print_status (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return FALSE;
    gboolean ok = print_status_on_connection (conn);
    g_object_unref (conn);
    return ok;
}

static gboolean
print_recent (gint max_lines)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return FALSE;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "GetRecentEvents",
        g_variant_new ("(i)", max_lines),
        G_VARIANT_TYPE ("(as)"),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (!ret) {
        g_printerr ("Call failed (is sg-daemon running?): %s\n",
                    err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        g_object_unref (conn);
        return FALSE;
    }
    GVariantIter *iter = NULL;
    g_variant_get (ret, "(as)", &iter);
    const gchar *line;
    while (g_variant_iter_loop (iter, "&s", &line)) {
        printf ("%s\n", line);
    }
    g_variant_iter_free (iter);
    g_variant_unref (ret);
    g_object_unref (conn);
    return TRUE;
}

static gboolean
do_reload (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return FALSE;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "ReloadConfig",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (!ret) {
        g_printerr ("Reload failed: %s\n", err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        g_object_unref (conn);
        return FALSE;
    } else {
        g_variant_unref (ret);
        g_print ("OK\n");
    }
    g_object_unref (conn);
    return TRUE;
}

static gint
print_health (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return 1;
    GError *error = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "GetDetailedStatus", NULL,
        G_VARIANT_TYPE ("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_object_unref (conn);
    if (!ret) {
        g_printerr ("Health call failed: %s\n", error ? error->message : "unknown");
        g_clear_error (&error);
        return 1;
    }
    GVariant *dict = NULL;
    g_variant_get (ret, "(@a{sv})", &dict);
    const gchar *webcam = "unavailable";
    const gchar *mic = "unavailable";
    const gchar *webcam_diag = "";
    const gchar *mic_diag = "";
    g_variant_lookup (dict, "webcam_health", "&s", &webcam);
    g_variant_lookup (dict, "mic_health", "&s", &mic);
    g_variant_lookup (dict, "webcam_diagnostic", "&s", &webcam_diag);
    g_variant_lookup (dict, "mic_diagnostic", "&s", &mic_diag);
    if (opt_json) {
        gchar *w = sg_json_escape (webcam);
        gchar *m = sg_json_escape (mic);
        gchar *wd = sg_json_escape (webcam_diag);
        gchar *md = sg_json_escape (mic_diag);
        g_print ("{\"webcam_health\":\"%s\",\"webcam_diagnostic\":\"%s\","
                 "\"mic_health\":\"%s\",\"mic_diagnostic\":\"%s\"}\n",
                 w, wd, m, md);
        g_free (w); g_free (m); g_free (wd); g_free (md);
    } else {
        g_print ("webcam_health: %s%s%s\n", webcam,
                 *webcam_diag ? " - " : "", webcam_diag);
        g_print ("mic_health: %s%s%s\n", mic, *mic_diag ? " - " : "", mic_diag);
    }
    gint rc = 0;
    if (g_strcmp0 (webcam, "unavailable") == 0 ||
        g_strcmp0 (mic, "unavailable") == 0) rc = 3;
    else if (g_strcmp0 (webcam, "ok") != 0 || g_strcmp0 (mic, "ok") != 0) rc = 2;
    g_variant_unref (dict);
    g_variant_unref (ret);
    return rc;
}

/* ---------- watch ---------- */

static guint watch_sub_id = 0;

static void
on_activity_changed (GDBusConnection *connection G_GNUC_UNUSED,
                     const gchar *sender_name G_GNUC_UNUSED,
                     const gchar *object_path G_GNUC_UNUSED,
                     const gchar *interface_name G_GNUC_UNUSED,
                     const gchar *signal_name G_GNUC_UNUSED,
                     GVariant *parameters,
                     gpointer user_data G_GNUC_UNUSED)
{
    gboolean w, m; const gchar *wp; const gchar *mp;
    g_variant_get (parameters, "(bb&s&s)", &w, &m, &wp, &mp);
    GDateTime *now = g_date_time_new_now_local ();
    gchar *ts = g_date_time_format (now, "%F %T");
    print_status_pair (w, m, wp, mp, ts);
    if (!opt_json) printf ("---\n");
    fflush (stdout);
    g_free (ts);
    g_date_time_unref (now);
}

static gboolean
quit_loop (gpointer data)
{
    g_main_loop_quit ((GMainLoop *) data);
    return G_SOURCE_REMOVE;
}

static gboolean
watch_status (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return FALSE;
    watch_sub_id = g_dbus_connection_signal_subscribe (
        conn, SG_BUS, SG_IFACE, "ActivityChanged", SG_PATH, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_activity_changed, NULL, NULL);
    if (!print_status_on_connection (conn)) {
        g_dbus_connection_signal_unsubscribe (conn, watch_sub_id);
        g_object_unref (conn);
        return FALSE;
    }
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_unix_signal_add (SIGINT,  quit_loop, loop);
    g_unix_signal_add (SIGTERM, quit_loop, loop);
    g_main_loop_run (loop);
    if (watch_sub_id) g_dbus_connection_signal_unsubscribe (conn, watch_sub_id);
    g_main_loop_unref (loop);
    g_object_unref (conn);
    return TRUE;
}

/* ---------- main ---------- */

static void
print_usage (FILE *out, const gchar *prog)
{
    fprintf (out,
             "Usage: %s [--json] [status|health|recent [N]|watch|reload]\n"
             "Commands:\n"
             "  status            Print current webcam/mic state (default)\n"
             "  health            Print monitor health (0=ok, 2=degraded, 3=unavailable)\n"
             "  recent [N]        Print last N (1..1000) log lines (default 100)\n"
             "  watch             Stream state changes\n"
             "  reload            Ask the daemon to reload its config\n"
             "Options:\n"
             "  --json            Machine-readable output (status / watch)\n"
             "  -h, --help        Show this help\n"
             "  -v, --version     Show version\n",
             prog);
}

int
main (int argc, char **argv)
{
    int write_index = 1;
    for (int read_index = 1; read_index < argc; read_index++) {
        if (g_strcmp0 (argv[read_index], "--json") == 0) {
            opt_json = TRUE;
        } else {
            argv[write_index++] = argv[read_index];
        }
    }
    argc = write_index;
    int i = 1;
    if (i < argc && (g_strcmp0 (argv[i], "-v") == 0 || g_strcmp0 (argv[i], "--version") == 0)) {
        g_print ("%s v%s developed by %s\n", SW_NAME, SNOOPGUARD_VERSION_FULL, DEV_NAME);
        return 0;
    }
    if (i < argc && (g_strcmp0 (argv[i], "-h") == 0 || g_strcmp0 (argv[i], "--help") == 0)) {
        print_usage (stdout, argv[0]);
        return 0;
    }
    if (i >= argc || g_strcmp0 (argv[i], "status") == 0) {
        if (i < argc && i + 1 < argc) return 2;
        return print_status () ? 0 : 1;
    }
    if (g_strcmp0 (argv[i], "health") == 0) {
        if (i + 1 < argc) return 2;
        return print_health ();
    }
    if (g_strcmp0 (argv[i], "recent") == 0) {
        gint n = 100;
        if (i + 1 < argc) {
            gchar *end = NULL;
            gint64 v = g_ascii_strtoll (argv[i + 1], &end, 10);
            if (!end || *end != '\0' || v < 1 || v > 1000) {
                g_printerr ("recent count must be between 1 and 1000\n");
                return 2;
            }
            n = (gint) v;
        }
        if (i + 2 < argc) return 2;
        return print_recent (n) ? 0 : 1;
    }
    if (g_strcmp0 (argv[i], "watch") == 0) {
        if (i + 1 < argc) return 2;
        return watch_status () ? 0 : 1;
    }
    if (g_strcmp0 (argv[i], "reload") == 0) {
        if (i + 1 < argc) return 2;
        return do_reload () ? 0 : 1;
    }
    print_usage (stderr, argv[0]);
    return 1;
}
