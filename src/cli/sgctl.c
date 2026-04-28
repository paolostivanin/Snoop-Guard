#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <string.h>
#include "version.h"

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
        gchar *jw = g_strescape (wp ? wp : "", NULL);
        gchar *jm = g_strescape (mp ? mp : "", NULL);
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

static void
print_status (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "GetStatus",
        NULL, G_VARIANT_TYPE ("(bbss)"),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (!ret) {
        g_printerr ("Call failed (is sg-daemon running?): %s\n",
                    err ? err->message : "unknown");
        if (err) g_clear_error (&err);
        g_object_unref (conn);
        return;
    }
    gboolean w, m; const gchar *wp; const gchar *mp;
    g_variant_get (ret, "(bbss)", &w, &m, &wp, &mp);
    print_status_pair (w, m, wp, mp, NULL);
    g_variant_unref (ret);
    g_object_unref (conn);
}

static void
print_recent (gint max_lines)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return;
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
        return;
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
}

static void
do_reload (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_sync (
        conn, SG_BUS, SG_PATH, SG_IFACE, "ReloadConfig",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (!ret) {
        g_printerr ("Reload failed: %s\n", err ? err->message : "unknown");
        if (err) g_clear_error (&err);
    } else {
        g_variant_unref (ret);
        g_print ("OK\n");
    }
    g_object_unref (conn);
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
    g_variant_get (parameters, "(bbss)", &w, &m, &wp, &mp);
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

static void
watch_status (void)
{
    GDBusConnection *conn = connect_session ();
    if (!conn) return;
    print_status ();
    watch_sub_id = g_dbus_connection_signal_subscribe (
        conn, SG_BUS, SG_IFACE, "ActivityChanged", SG_PATH, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_activity_changed, NULL, NULL);
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_unix_signal_add (SIGINT,  quit_loop, loop);
    g_unix_signal_add (SIGTERM, quit_loop, loop);
    g_main_loop_run (loop);
    if (watch_sub_id) g_dbus_connection_signal_unsubscribe (conn, watch_sub_id);
    g_main_loop_unref (loop);
    g_object_unref (conn);
}

/* ---------- main ---------- */

static void
print_usage (FILE *out, const gchar *prog)
{
    fprintf (out,
             "Usage: %s [--json] [status|recent [N]|watch|reload]\n"
             "Commands:\n"
             "  status            Print current webcam/mic state (default)\n"
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
    int i = 1;
    if (i < argc && (g_strcmp0 (argv[i], "--json") == 0)) {
        opt_json = TRUE;
        i++;
    }
    if (i < argc && (g_strcmp0 (argv[i], "-v") == 0 || g_strcmp0 (argv[i], "--version") == 0)) {
        g_print ("%s v%s developed by %s\n", SW_NAME, SNOOPGUARD_VERSION_FULL, DEV_NAME);
        return 0;
    }
    if (i < argc && (g_strcmp0 (argv[i], "-h") == 0 || g_strcmp0 (argv[i], "--help") == 0)) {
        print_usage (stdout, argv[0]);
        return 0;
    }
    if (i >= argc || g_strcmp0 (argv[i], "status") == 0) {
        print_status ();
        return 0;
    }
    if (g_strcmp0 (argv[i], "recent") == 0) {
        gint n = 100;
        if (i + 1 < argc) {
            gchar *end = NULL;
            gint64 v = g_ascii_strtoll (argv[i + 1], &end, 10);
            if (end && *end == '\0' && v > 0) n = (gint) v;
        }
        print_recent (n);
        return 0;
    }
    if (g_strcmp0 (argv[i], "watch") == 0) {
        watch_status ();
        return 0;
    }
    if (g_strcmp0 (argv[i], "reload") == 0) {
        do_reload ();
        return 0;
    }
    print_usage (stderr, argv[0]);
    return 1;
}
