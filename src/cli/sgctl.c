#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include "version.h"

static void print_status(void) {
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) { g_printerr("D-Bus error: %s\n", err ? err->message : "unknown"); g_clear_error(&err); return; }
    GVariant *ret = g_dbus_connection_call_sync(conn,
                                                "org.snoopguard.Service",
                                                "/org/snoopguard/Service",
                                                "org.snoopguard.Service",
                                                "GetStatus",
                                                NULL,
                                                G_VARIANT_TYPE("(bbss)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                &err);
    if (!ret) { g_printerr("Call failed: %s\n", err ? err->message : "unknown"); g_clear_error(&err); g_object_unref(conn); return; }
    gboolean w, m; const gchar *wp; const gchar *mp;
    g_variant_get(ret, "(bbss)", &w, &m, &wp, &mp);
    printf("webcam_active: %s", w ? "true" : "false"); if (wp && *wp) printf(" (proc: %s)", wp); printf("\n");
    printf("mic_active: %s", m ? "true" : "false"); if (mp && *mp) printf(" (proc: %s)", mp); printf("\n");
    g_variant_unref(ret);
    g_object_unref(conn);
}

static void print_recent(int max_lines) {
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) { g_printerr("D-Bus error: %s\n", err ? err->message : "unknown"); g_clear_error(&err); return; }
    GVariant *ret = g_dbus_connection_call_sync(conn,
                                                "org.snoopguard.Service",
                                                "/org/snoopguard/Service",
                                                "org.snoopguard.Service",
                                                "GetRecentEvents",
                                                g_variant_new("(i)", max_lines),
                                                G_VARIANT_TYPE("(as)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                &err);
    if (!ret) { g_printerr("Call failed: %s\n", err ? err->message : "unknown"); g_clear_error(&err); g_object_unref(conn); return; }
    GVariantIter *iter; gchar *line;
    g_variant_get(ret, "(as)", &iter);
    while (g_variant_iter_loop(iter, "s", &line)) {
        printf("%s\n", line);
    }
    g_variant_iter_free(iter);
    g_variant_unref(ret);
    g_object_unref(conn);
}

static void on_activity_changed(GDBusConnection *connection __attribute__((unused)),
                                const gchar *sender_name __attribute__((unused)),
                                const gchar *object_path __attribute__((unused)),
                                const gchar *interface_name __attribute__((unused)),
                                const gchar *signal_name __attribute__((unused)),
                                GVariant *parameters,
                                gpointer user_data __attribute__((unused))) {
    gboolean w, m; const gchar *wp; const gchar *mp;
    g_variant_get(parameters, "(bbss)", &w, &m, &wp, &mp);
    printf("webcam_active: %s", w ? "true" : "false"); if (wp && *wp) printf(" (proc: %s)", wp); printf("\n");
    printf("mic_active: %s", m ? "true" : "false"); if (mp && *mp) printf(" (proc: %s)", mp); printf("\n");
    printf("---\n");
}

static gboolean quit_loop(gpointer data) {
    GMainLoop *loop = data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void watch_status(void) {
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) { g_printerr("D-Bus error: %s\n", err ? err->message : "unknown"); g_clear_error(&err); return; }
    print_status();
    g_dbus_connection_signal_subscribe(conn,
                                       "org.snoopguard.Service",
                                       "org.snoopguard.Service",
                                       "ActivityChanged",
                                       "/org/snoopguard/Service",
                                       NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_activity_changed,
                                       NULL,
                                       NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, quit_loop, loop);
    g_unix_signal_add(SIGTERM, quit_loop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_object_unref(conn);
}

int main(int argc, char **argv) {
    if (argc > 1 && (g_strcmp0(argv[1], "-v") == 0 || g_strcmp0(argv[1], "--version") == 0)) {
        g_print("%s v%s developed by %s\n", SW_NAME, SNOOPGUARD_VERSION_FULL, DEV_NAME);
        return 0;
    }
    if (argc > 1 && (g_strcmp0(argv[1], "-h") == 0 || g_strcmp0(argv[1], "--help") == 0)) {
        fprintf(stderr, "Usage: %s [status|recent [N]|watch]\n\nOptions:\n  -h, --help       Show this help\n  -v, --version    Show version\n", argv[0]);
        return 0;
    }
    if (argc <= 1 || g_strcmp0(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    if (g_strcmp0(argv[1], "recent") == 0) {
        int n = 100;
        if (argc > 2) n = (int)g_ascii_strtoll(argv[2], NULL, 10);
        print_recent(n);
        return 0;
    }
    if (g_strcmp0(argv[1], "watch") == 0) {
        watch_status();
        return 0;
    }
    fprintf(stderr, "Usage: %s [status|recent [N]|watch]\n", argv[0]);
    return 1;
}
