#include "sg-dbus.h"
#include <string.h>

#define SG_BUS_NAME "org.snoopguard.Service"
#define SG_OBJECT_PATH "/org/snoopguard/Service"
#define SG_INTERFACE "org.snoopguard.Service"

static GDBusNodeInfo *introspection_data = NULL;
static GDBusConnection *bus_conn = NULL;
static guint reg_id = 0;
static SGStatus current_status = { FALSE, FALSE, NULL, NULL };

static const gchar introspection_xml[] =
        "<node>"
        "  <interface name='org.snoopguard.Service'>"
        "    <method name='GetStatus'>"
        "      <arg type='b' name='webcam_active' direction='out'/>"
        "      <arg type='b' name='mic_active' direction='out'/>"
        "      <arg type='s' name='webcam_proc' direction='out'/>"
        "      <arg type='s' name='mic_proc' direction='out'/>"
        "    </method>"
        "    <method name='GetRecentEvents'>"
        "      <arg type='i' name='max_lines' direction='in'/>"
        "      <arg type='as' name='lines' direction='out'/>"
        "    </method>"
        "    <signal name='ActivityChanged'>"
        "      <arg type='b' name='webcam_active'/>"
        "      <arg type='b' name='mic_active'/>"
        "      <arg type='s' name='webcam_proc'/>"
        "      <arg type='s' name='mic_proc'/>"
        "    </signal>"
        "  </interface>"
        "</node>";

static void handle_method_call(GDBusConnection *connection,
                               const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)user_data;
    if (g_strcmp0(method_name, "GetStatus") == 0) {
        const gchar *wproc = current_status.webcam_proc ? current_status.webcam_proc : "";
        const gchar *mproc = current_status.mic_proc ? current_status.mic_proc : "";
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(bbss)",
                                                           current_status.webcam_active,
                                                           current_status.mic_active,
                                                           wproc,
                                                           mproc));
    } else if (g_strcmp0(method_name, "GetRecentEvents") == 0) {
        gint max_lines = 100;
        g_variant_get(parameters, "(i)", &max_lines);
        if (max_lines <= 0) max_lines = 100; else if (max_lines > 1000) max_lines = 1000;
        // naive tail implementation: read user log file and return last N lines
        gchar *log_path = g_build_filename(g_get_user_state_dir(), "snoop-guard", "events.log", NULL);
        gchar *contents = NULL;
        gsize len = 0;
        GError *err = NULL;
        if (!g_file_get_contents(log_path, &contents, &len, &err) || contents == NULL || len == 0) {
            if (err) g_clear_error(&err);
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &b));
            g_free(log_path);
            if (contents) g_free(contents);
            return;
        }
        GPtrArray *lines = g_ptr_array_new_with_free_func(g_free);
        gchar **split = g_strsplit(contents, "\n", -1);
        for (guint i = 0; split[i] != NULL; i++) {
            if (split[i][0] == '\0') continue;
            g_ptr_array_add(lines, g_strdup(split[i]));
        }
        gint start = (gint)lines->len - max_lines;
        if (start < 0) start = 0;
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        for (guint i = (guint)start; i < lines->len; i++) {
            g_variant_builder_add(&b, "s", (gchar*)g_ptr_array_index(lines, i));
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &b));
        g_strfreev(split);
        g_ptr_array_free(lines, TRUE);
        g_free(contents);
        g_free(log_path);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
        .method_call = handle_method_call,
        .get_property = NULL,
        .set_property = NULL,
        .padding = { 0 }
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name; (void)user_data;
    bus_conn = connection;
    reg_id = g_dbus_connection_register_object(connection,
                                               SG_OBJECT_PATH,
                                               introspection_data->interfaces[0],
                                               &interface_vtable,
                                               NULL, NULL, NULL);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection; (void)name; (void)user_data;
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection; (void)name; (void)user_data;
}

void sg_dbus_init(GMainLoop *loop) {
    (void)loop;
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_bus_own_name(G_BUS_TYPE_SESSION,
                   SG_BUS_NAME,
                   G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus_acquired,
                   on_name_acquired,
                   on_name_lost,
                   NULL,
                   NULL);
}

void sg_dbus_update_status(const SGStatus *status) {
    if (!bus_conn) return;
    if (current_status.webcam_proc) g_free(current_status.webcam_proc);
    if (current_status.mic_proc) g_free(current_status.mic_proc);
    current_status = *status; // shallow copy
    if (status->webcam_proc) current_status.webcam_proc = g_strdup(status->webcam_proc);
    if (status->mic_proc) current_status.mic_proc = g_strdup(status->mic_proc);

    GVariant *params = g_variant_new("(bbss)",
                                     current_status.webcam_active,
                                     current_status.mic_active,
                                     current_status.webcam_proc ? current_status.webcam_proc : "",
                                     current_status.mic_proc ? current_status.mic_proc : "");
    g_dbus_connection_emit_signal(bus_conn,
                                  NULL,
                                  SG_OBJECT_PATH,
                                  SG_INTERFACE,
                                  "ActivityChanged",
                                  params,
                                  NULL);
}
