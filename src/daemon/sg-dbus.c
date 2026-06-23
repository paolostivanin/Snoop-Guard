#include "sg-dbus.h"
#include "sg-logging.h"
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define SG_BUS_NAME    "org.snoopguard.Service"
#define SG_OBJECT_PATH "/org/snoopguard/Service"
#define SG_INTERFACE   "org.snoopguard.Service"
#define GET_RECENT_TAIL_MAX_BYTES (G_GOFFSET_CONSTANT (256) * 1024)

static GDBusNodeInfo *introspection_data = NULL;
static GDBusConnection *bus_conn = NULL;
static guint owner_id = 0;
static guint reg_id   = 0;
static SGStatus current_status = { 0 };
static SGReloadFn reload_cb = NULL;
static gpointer   reload_cb_data = NULL;
static const gchar *empty_strv[] = { NULL };
static gboolean fatal_bus_error = FALSE;

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
    "    <method name='GetDetailedStatus'>"
    "      <arg type='a{sv}' name='status' direction='out'/>"
    "    </method>"
    "    <method name='ReloadConfig'/>"
    "    <signal name='ActivityChanged'>"
    "      <arg type='b' name='webcam_active'/>"
    "      <arg type='b' name='mic_active'/>"
    "      <arg type='s' name='webcam_proc'/>"
    "      <arg type='s' name='mic_proc'/>"
    "    </signal>"
    "    <signal name='DetailedStatusChanged'>"
    "      <arg type='a{sv}' name='status'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static gboolean
strv_equal_nullable (gchar **a, gchar **b)
{
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    return g_strv_equal ((const gchar * const *) a,
                         (const gchar * const *) b);
}

static const gchar * const *
nonnull_strv (gchar **values)
{
    return values ? (const gchar * const *) values : empty_strv;
}

/* ---------- helpers ---------- */

static void
emit_status_signal (void)
{
    if (!bus_conn) return;
    GVariant *params = g_variant_new ("(bbss)",
                                      current_status.webcam_active,
                                      current_status.mic_active,
                                      current_status.webcam_proc ? current_status.webcam_proc : "",
                                      current_status.mic_proc    ? current_status.mic_proc    : "");
    g_dbus_connection_emit_signal (bus_conn, NULL,
                                   SG_OBJECT_PATH, SG_INTERFACE,
                                   "ActivityChanged", params, NULL);
}

static GVariant *
build_detailed_status (void)
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&builder, "{sv}", "schema_version",
                           g_variant_new_uint32 (1));
    g_variant_builder_add (&builder, "{sv}", "webcam_active",
                           g_variant_new_boolean (current_status.webcam_active));
    g_variant_builder_add (&builder, "{sv}", "mic_active",
                           g_variant_new_boolean (current_status.mic_active));
    g_variant_builder_add (&builder, "{sv}", "webcam_health",
                           g_variant_new_string (current_status.webcam_health
                                                 ? current_status.webcam_health : "unavailable"));
    g_variant_builder_add (&builder, "{sv}", "mic_health",
                           g_variant_new_string (current_status.mic_health
                                                 ? current_status.mic_health : "unavailable"));
    g_variant_builder_add (&builder, "{sv}", "webcam_processes",
                           g_variant_new_strv (nonnull_strv (
                                              current_status.webcam_processes), -1));
    g_variant_builder_add (&builder, "{sv}", "mic_processes",
                           g_variant_new_strv (nonnull_strv (
                                              current_status.mic_processes), -1));
    g_variant_builder_add (&builder, "{sv}", "webcam_unknown_devices",
                           g_variant_new_strv (nonnull_strv (
                                              current_status.webcam_unknown_devices), -1));
    g_variant_builder_add (&builder, "{sv}", "webcam_diagnostic",
                           g_variant_new_string (current_status.webcam_diagnostic
                                                 ? current_status.webcam_diagnostic : ""));
    g_variant_builder_add (&builder, "{sv}", "mic_diagnostic",
                           g_variant_new_string (current_status.mic_diagnostic
                                                 ? current_status.mic_diagnostic : ""));
    return g_variant_builder_end (&builder);
}

static void
emit_detailed_status_signal (void)
{
    if (!bus_conn) return;
    g_dbus_connection_emit_signal (
        bus_conn, NULL, SG_OBJECT_PATH, SG_INTERFACE, "DetailedStatusChanged",
        g_variant_new ("(@a{sv})", build_detailed_status ()), NULL);
}

/* Read up to GET_RECENT_TAIL_MAX_BYTES from the end of path; return last
 * `max_lines` non-empty lines as a newly-allocated NULL-terminated array. */
static gchar **
read_log_tail (const gchar *path, gint max_lines)
{
    if (!path) return g_new0 (gchar *, 1);
    int fd = g_open (path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    if (fd < 0) return g_new0 (gchar *, 1);
    GStatBuf st;
    if (fstat (fd, &st) != 0 || !S_ISREG (st.st_mode)) {
        close (fd);
        return g_new0 (gchar *, 1);
    }
    goffset start = 0;
    if ((goffset) st.st_size > GET_RECENT_TAIL_MAX_BYTES) {
        start = (goffset) st.st_size - GET_RECENT_TAIL_MAX_BYTES;
    }
    if (lseek (fd, start, SEEK_SET) == (off_t) -1) {
        (void) close (fd);
        return g_new0 (gchar *, 1);
    }
    gsize cap = (gsize) (st.st_size - start);
    gchar *buf = g_malloc (cap + 1);
    gsize got = 0;
    while (got < cap) {
        ssize_t r = read (fd, buf + got, cap - got);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        got += (gsize) r;
    }
    (void) close (fd);
    buf[got] = '\0';

    /* Skip a partial first line if we started mid-stream. */
    gchar *p = buf;
    if (start > 0) {
        gchar *nl = strchr (buf, '\n');
        if (nl) p = nl + 1;
        else    p = buf + got; /* nothing usable */
    }
    gchar **split = g_strsplit (p, "\n", -1);
    g_free (buf);

    /* Drop empty entries; keep order. */
    GPtrArray *kept = g_ptr_array_new_with_free_func (g_free);
    for (guint i = 0; split[i] != NULL; i++) {
        if (split[i][0] == '\0') continue;
        g_ptr_array_add (kept, g_utf8_make_valid (split[i], -1));
    }
    g_strfreev (split);

    guint start_idx = 0;
    if ((gint) kept->len > max_lines) {
        start_idx = kept->len - (guint) max_lines;
    }
    gchar **out = g_new0 (gchar *, kept->len - start_idx + 1);
    guint k = 0;
    for (guint i = start_idx; i < kept->len; i++) {
        out[k++] = g_strdup (g_ptr_array_index (kept, i));
    }
    g_ptr_array_free (kept, TRUE);
    return out;
}

/* ---------- method dispatch ---------- */

static void
handle_method_call (GDBusConnection *connection G_GNUC_UNUSED,
                    const gchar *sender G_GNUC_UNUSED,
                    const gchar *object_path G_GNUC_UNUSED,
                    const gchar *interface_name G_GNUC_UNUSED,
                    const gchar *method_name,
                    GVariant *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer user_data G_GNUC_UNUSED)
{
    if (g_strcmp0 (method_name, "GetStatus") == 0) {
        const gchar *wproc = current_status.webcam_proc ? current_status.webcam_proc : "";
        const gchar *mproc = current_status.mic_proc    ? current_status.mic_proc    : "";
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(bbss)",
                           current_status.webcam_active,
                           current_status.mic_active,
                           wproc, mproc));
        return;
    }
    if (g_strcmp0 (method_name, "GetRecentEvents") == 0) {
        gint max_lines = 100;
        g_variant_get (parameters, "(i)", &max_lines);
        if (max_lines <= 0)   max_lines = 100;
        if (max_lines > 1000) max_lines = 1000;
        gchar **lines = read_log_tail (sg_log_get_path (), max_lines);
        GVariant *line_array = g_variant_new_strv (
            (const gchar * const *) lines, -1);
        g_strfreev (lines);
        g_dbus_method_invocation_return_value (
            invocation, g_variant_new ("(@as)", line_array));
        return;
    }
    if (g_strcmp0 (method_name, "GetDetailedStatus") == 0) {
        g_dbus_method_invocation_return_value (
            invocation, g_variant_new ("(@a{sv})", build_detailed_status ()));
        return;
    }
    if (g_strcmp0 (method_name, "ReloadConfig") == 0) {
        GError *error = NULL;
        if (reload_cb && !reload_cb (reload_cb_data, &error)) {
            g_dbus_method_invocation_return_gerror (invocation, error);
            g_clear_error (&error);
            return;
        }
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }
    g_dbus_method_invocation_return_error (invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
        "Unknown method '%s'", method_name);
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
    .padding      = { 0 },
};

/* ---------- bus name handling ---------- */

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar *name G_GNUC_UNUSED,
                 gpointer user_data G_GNUC_UNUSED)
{
    bus_conn = connection;
    GError *err = NULL;
    reg_id = g_dbus_connection_register_object (connection,
                                                SG_OBJECT_PATH,
                                                introspection_data->interfaces[0],
                                                &interface_vtable,
                                                NULL, NULL, &err);
    if (reg_id == 0) {
        g_warning ("Failed to register D-Bus object: %s",
                   err ? err->message : "(unknown)");
        if (err) g_clear_error (&err);
    }
}

static void
on_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                  const gchar *name G_GNUC_UNUSED,
                  gpointer user_data G_GNUC_UNUSED)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data G_GNUC_UNUSED)
{
    /* Either we couldn't connect to the session bus, or another instance
     * already owns the name. Either way, loudly bail. */
    g_warning ("Lost D-Bus name '%s' (connection %s). Exiting.",
               name ? name : SG_BUS_NAME,
               connection ? "ok" : "missing");
    fatal_bus_error = TRUE;
    /* Trigger graceful shutdown via SIGTERM to ourselves so main can clean up. */
    raise (SIGTERM);
}

/* ---------- public API ---------- */

void
sg_dbus_init (SGReloadFn cb, gpointer cb_data)
{
    fatal_bus_error = FALSE;
    reload_cb      = cb;
    reload_cb_data = cb_data;
    GError *err = NULL;
    introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, &err);
    if (!introspection_data) {
        g_warning ("Failed to parse D-Bus introspection: %s",
                   err ? err->message : "(unknown)");
        if (err) g_clear_error (&err);
        return;
    }
    owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                               SG_BUS_NAME,
                               G_BUS_NAME_OWNER_FLAGS_NONE,
                               on_bus_acquired,
                               on_name_acquired,
                               on_name_lost,
                               NULL, NULL);
}

void
sg_dbus_uninit (void)
{
    if (reg_id && bus_conn) {
        g_dbus_connection_unregister_object (bus_conn, reg_id);
        reg_id = 0;
    }
    if (owner_id) {
        g_bus_unown_name (owner_id);
        owner_id = 0;
    }
    g_clear_pointer (&introspection_data, g_dbus_node_info_unref);
    g_clear_pointer (&current_status.webcam_proc, g_free);
    g_clear_pointer (&current_status.mic_proc, g_free);
    g_clear_pointer (&current_status.webcam_processes, g_strfreev);
    g_clear_pointer (&current_status.mic_processes, g_strfreev);
    g_clear_pointer (&current_status.webcam_unknown_devices, g_strfreev);
    g_clear_pointer (&current_status.webcam_health, g_free);
    g_clear_pointer (&current_status.mic_health, g_free);
    g_clear_pointer (&current_status.webcam_diagnostic, g_free);
    g_clear_pointer (&current_status.mic_diagnostic, g_free);
    bus_conn = NULL;
}

void
sg_dbus_update_status (const SGStatus *status)
{
    if (!status) return;
    gboolean changed = (current_status.webcam_active != status->webcam_active) ||
                       (current_status.mic_active    != status->mic_active)    ||
                       (g_strcmp0 (current_status.webcam_proc, status->webcam_proc) != 0) ||
                       (g_strcmp0 (current_status.mic_proc,    status->mic_proc)    != 0) ||
                       !strv_equal_nullable (current_status.webcam_processes,
                                            status->webcam_processes) ||
                       !strv_equal_nullable (current_status.mic_processes,
                                            status->mic_processes) ||
                       !strv_equal_nullable (current_status.webcam_unknown_devices,
                                            status->webcam_unknown_devices) ||
                       g_strcmp0 (current_status.webcam_health, status->webcam_health) != 0 ||
                       g_strcmp0 (current_status.mic_health, status->mic_health) != 0 ||
                       g_strcmp0 (current_status.webcam_diagnostic,
                                  status->webcam_diagnostic) != 0 ||
                       g_strcmp0 (current_status.mic_diagnostic,
                                  status->mic_diagnostic) != 0;

    current_status.webcam_active = status->webcam_active;
    current_status.mic_active    = status->mic_active;
    g_clear_pointer (&current_status.webcam_proc, g_free);
    g_clear_pointer (&current_status.mic_proc, g_free);
    g_clear_pointer (&current_status.webcam_processes, g_strfreev);
    g_clear_pointer (&current_status.mic_processes, g_strfreev);
    g_clear_pointer (&current_status.webcam_unknown_devices, g_strfreev);
    g_free (current_status.webcam_health);
    g_free (current_status.mic_health);
    g_free (current_status.webcam_diagnostic);
    g_free (current_status.mic_diagnostic);
    if (status->webcam_proc) current_status.webcam_proc = g_strdup (status->webcam_proc);
    if (status->mic_proc)    current_status.mic_proc    = g_strdup (status->mic_proc);
    current_status.webcam_processes = g_strdupv (status->webcam_processes);
    current_status.mic_processes = g_strdupv (status->mic_processes);
    current_status.webcam_unknown_devices = g_strdupv (status->webcam_unknown_devices);
    current_status.webcam_health = g_strdup (status->webcam_health);
    current_status.mic_health = g_strdup (status->mic_health);
    current_status.webcam_diagnostic = g_strdup (status->webcam_diagnostic);
    current_status.mic_diagnostic = g_strdup (status->mic_diagnostic);

    if (changed) {
        emit_status_signal ();
        emit_detailed_status_signal ();
    }
}

GDBusConnection *
sg_dbus_get_bus (void)
{
    return bus_conn;
}

gboolean
sg_dbus_had_fatal_error (void)
{
    return fatal_bus_error;
}
