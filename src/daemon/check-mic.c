#include <glib.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>
#include <string.h>
#include "main.h"

typedef struct {
    uint32_t id;
    struct pw_node *proxy;
    struct spa_hook listener;
    /* Cached attributes from info events. */
    gchar *media_class;
    gchar *node_name;
    gchar *app_name;
    gchar *app_binary;
    gboolean active; /* RUNNING capture node matching filter */
    /* Backref so node listener can see the global monitor state. */
    struct MicMonitor *monitor;
} PwNode;

typedef struct MicMonitor {
    /* PipeWire */
    struct pw_main_loop *pw_loop_handle;
    struct pw_loop      *pw_loop;
    struct pw_context   *context;
    struct pw_core      *core;
    struct spa_hook      core_listener;
    struct pw_registry  *registry;
    struct spa_hook      registry_listener;
    GHashTable          *nodes; /* uint32_t id -> PwNode* */
    guint                pw_source_id;
    /* Config */
    gchar               *filter; /* may be NULL/empty */
    /* Callback into main */
    SGMicChangedFn       cb;
    gpointer             cb_data;
    /* Snapshot of last reported state, to suppress duplicate callbacks. */
    gboolean             last_active;
    gchar              **last_processes;
    SGMonitorHealth      health;
    gchar               *diagnostic;
} MicMonitor;

static MicMonitor *g_monitor = NULL;

/* ---------- helpers ---------- */

static gint
compare_strings (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (*(gchar * const *) a, *(gchar * const *) b);
}

static gboolean
strv_equal_nullable (gchar **a, gchar **b)
{
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    return g_strv_equal ((const gchar * const *) a,
                         (const gchar * const *) b);
}

static gboolean
contains_ci (const gchar *hay, const gchar *needle)
{
    if (!hay || !needle || !*needle) return FALSE;
    gchar *h = g_utf8_casefold (hay, -1);
    gchar *n = g_utf8_casefold (needle, -1);
    gboolean found = (strstr (h, n) != NULL);
    g_free (h);
    g_free (n);
    return found;
}

static gchar *
safe_metadata (const gchar *value)
{
    if (!value) return NULL;
    gchar *safe = g_utf8_make_valid (value, -1);
    for (gchar *p = safe; *p; p++) {
        if ((guchar) *p < 0x20 || (guchar) *p == 0x7f) *p = ' ';
    }
    g_strstrip (safe);
    if (!*safe) g_clear_pointer (&safe, g_free);
    return safe;
}

static gboolean
is_capture_class (const gchar *media_class)
{
    if (!media_class) return FALSE;
    return strstr (media_class, "Audio/Source") != NULL ||
           strstr (media_class, "Stream/Input/Audio") != NULL;
}

static gboolean
is_capture_stream (const gchar *media_class)
{
    return media_class &&
           strstr (media_class, "Stream/Input/Audio") != NULL;
}

static gboolean
node_matches_filter (const PwNode *n, const gchar *filter)
{
    if (!filter || !*filter) return TRUE;
    return contains_ci (n->node_name, filter) ||
           contains_ci (n->app_name, filter) ||
           contains_ci (n->app_binary, filter);
}

static void
pwnode_free (gpointer p)
{
    PwNode *n = p;
    if (!n) return;
    spa_hook_remove (&n->listener);
    if (n->proxy) pw_proxy_destroy ((struct pw_proxy *) n->proxy);
    g_free (n->media_class);
    g_free (n->node_name);
    g_free (n->app_name);
    g_free (n->app_binary);
    g_free (n);
}

/* Recompute the aggregate (any active capture matching filter?) and notify
 * main if it changed. */
static void
recompute_and_dispatch (MicMonitor *m)
{
    gboolean any_active = FALSE;
    GHashTable *processes = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init (&it, m->nodes);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        PwNode *n = value;
        if (n->active) {
            any_active = TRUE;
            if (is_capture_stream (n->media_class)) {
                const gchar *proc_name = n->app_binary ? n->app_binary :
                                         n->app_name   ? n->app_name   :
                                         n->node_name;
                if (proc_name && *proc_name) {
                    g_hash_table_add (processes, g_strdup (proc_name));
                }
            }
        }
    }
    GPtrArray *array = g_ptr_array_new_with_free_func (g_free);
    g_hash_table_iter_init (&it, processes);
    while (g_hash_table_iter_next (&it, &key, NULL)) {
        g_ptr_array_add (array, g_strdup (key));
    }
    g_ptr_array_sort (array, compare_strings);
    g_ptr_array_add (array, NULL);
    gchar **process_strv = (gchar **) g_ptr_array_free (array, FALSE);
    gboolean changed = (m->last_active != any_active) ||
                       !strv_equal_nullable (m->last_processes, process_strv);
    if (!changed) {
        g_strfreev (process_strv);
        g_hash_table_destroy (processes);
        return;
    }
    m->last_active = any_active;
    g_clear_pointer (&m->last_processes, g_strfreev);
    m->last_processes = g_strdupv (process_strv);
    if (m->cb) {
        SGMonitorSnapshot snapshot = {
            .active = any_active,
            .processes = process_strv,
            .health = m->health,
            .diagnostic = m->diagnostic,
        };
        m->cb (&snapshot, m->cb_data);
    }
    g_strfreev (process_strv);
    g_hash_table_destroy (processes);
}

static void
update_from_props (PwNode *n, const struct spa_dict *props)
{
    if (!props) return;
    const gchar *mc = spa_dict_lookup (props, PW_KEY_MEDIA_CLASS);
    const gchar *nn = spa_dict_lookup (props, PW_KEY_NODE_NAME);
    const gchar *an = spa_dict_lookup (props, PW_KEY_APP_NAME);
    const gchar *ab = spa_dict_lookup (props, PW_KEY_APP_PROCESS_BINARY);
    if (mc) { g_free (n->media_class); n->media_class = safe_metadata (mc); }
    if (nn) { g_free (n->node_name);   n->node_name   = safe_metadata (nn); }
    if (an) { g_free (n->app_name);    n->app_name    = safe_metadata (an); }
    if (ab) { g_free (n->app_binary);  n->app_binary  = safe_metadata (ab); }
}

/* ---------- node events ---------- */

static void
on_node_info (void *data, const struct pw_node_info *info)
{
    PwNode *n = data;
    if (!info) return;
    update_from_props (n, info->props);
    gboolean is_capture = is_capture_class (n->media_class);
    gboolean running    = (info->state == PW_NODE_STATE_RUNNING);
    gboolean matches    = node_matches_filter (n, n->monitor->filter);
    n->active = is_capture && running && matches;
    recompute_and_dispatch (n->monitor);
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = on_node_info,
};

/* ---------- registry events ---------- */

static void
on_registry_global (void *data, uint32_t id, uint32_t permissions,
                    const char *type, uint32_t version,
                    const struct spa_dict *props)
{
    (void) permissions;
    (void) props;
    MicMonitor *m = data;
    if (!type || strcmp (type, PW_TYPE_INTERFACE_Node) != 0) return;

    PwNode *n = g_new0 (PwNode, 1);
    n->id      = id;
    n->monitor = m;
    n->proxy   = pw_registry_bind (m->registry, id, type, version, 0);
    if (!n->proxy) {
        g_free (n);
        return;
    }
    pw_node_add_listener (n->proxy, &n->listener, &node_events, n);
    g_hash_table_insert (m->nodes, GUINT_TO_POINTER (id), n);
}

static void
on_registry_global_remove (void *data, uint32_t id)
{
    MicMonitor *m = data;
    g_hash_table_remove (m->nodes, GUINT_TO_POINTER (id));
    recompute_and_dispatch (m);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global        = on_registry_global,
    .global_remove = on_registry_global_remove,
};

static void
on_core_error (void *data,
               uint32_t id G_GNUC_UNUSED,
               int seq G_GNUC_UNUSED,
               int res,
               const char *message)
{
    MicMonitor *m = data;
    if (res >= 0) return;
    m->health = SG_MONITOR_UNAVAILABLE;
    g_free (m->diagnostic);
    m->diagnostic = g_strdup_printf ("PipeWire connection failed: %s",
                                     message ? message : g_strerror (-res));
    if (m->cb) {
        SGMonitorSnapshot snapshot = {
            .active = FALSE,
            .health = m->health,
            .diagnostic = m->diagnostic,
        };
        m->cb (&snapshot, m->cb_data);
    }
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .error = on_core_error,
};

/* ---------- GLib integration ---------- */

static gboolean
on_pw_fd_ready (gint fd, GIOCondition cond, gpointer data)
{
    (void) fd;
    MicMonitor *m = data;
    if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        m->pw_source_id = 0;
        on_core_error (m, 0, 0, EPIPE, "PipeWire event loop disconnected");
        return G_SOURCE_REMOVE;
    }
    int r = pw_loop_iterate (m->pw_loop, 0);
    if (r < 0) {
        g_warning ("pw_loop_iterate: %s", g_strerror (-r));
    }
    return G_SOURCE_CONTINUE;
}

/* ---------- public API ---------- */

gboolean
mic_monitor_init (const gchar *filter, SGMicChangedFn cb, gpointer user_data)
{
    if (g_monitor) return TRUE;

    pw_init (NULL, NULL);

    MicMonitor *m = g_new0 (MicMonitor, 1);
    m->cb       = cb;
    m->cb_data  = user_data;
    m->filter   = (filter && *filter) ? g_strdup (filter) : NULL;
    m->nodes    = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                         NULL, pwnode_free);
    m->last_active = FALSE;
    m->health = SG_MONITOR_OK;

    m->pw_loop_handle = pw_main_loop_new (NULL);
    if (!m->pw_loop_handle) goto fail;
    m->pw_loop = pw_main_loop_get_loop (m->pw_loop_handle);

    m->context = pw_context_new (m->pw_loop, NULL, 0);
    if (!m->context) goto fail;

    m->core = pw_context_connect (m->context, NULL, 0);
    if (!m->core) {
        g_warning ("PipeWire connect failed; mic monitoring disabled");
        goto fail;
    }
    pw_core_add_listener (m->core, &m->core_listener, &core_events, m);

    m->registry = pw_core_get_registry (m->core, PW_VERSION_REGISTRY, 0);
    if (!m->registry) goto fail;
    pw_registry_add_listener (m->registry, &m->registry_listener,
                              &registry_events, m);

    int fd = pw_loop_get_fd (m->pw_loop);
    if (fd < 0) goto fail;
    m->pw_source_id = g_unix_fd_add (fd, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                     on_pw_fd_ready, m);

    /* Claim the loop for this thread (the main thread). */
    pw_loop_enter (m->pw_loop);

    g_monitor = m;
    if (m->cb) {
        SGMonitorSnapshot snapshot = {
            .active = FALSE,
            .health = SG_MONITOR_OK,
        };
        m->cb (&snapshot, m->cb_data);
    }
    return TRUE;

fail:
    if (m->registry) pw_proxy_destroy ((struct pw_proxy *) m->registry);
    if (m->core) {
        spa_hook_remove (&m->core_listener);
        pw_core_disconnect (m->core);
    }
    if (m->context)  pw_context_destroy (m->context);
    if (m->pw_loop_handle) pw_main_loop_destroy (m->pw_loop_handle);
    g_hash_table_destroy (m->nodes);
    g_free (m->filter);
    g_free (m);
    pw_deinit ();
    return FALSE;
}

void
mic_monitor_uninit (void)
{
    MicMonitor *m = g_monitor;
    if (!m) return;
    g_monitor = NULL;
    if (m->pw_source_id) g_source_remove (m->pw_source_id);
    /* Destroy nodes first so their listeners detach before the proxies become
     * invalid via core disconnect. */
    g_hash_table_destroy (m->nodes);
    if (m->registry) pw_proxy_destroy ((struct pw_proxy *) m->registry);
    spa_hook_remove (&m->core_listener);
    if (m->core)     pw_core_disconnect (m->core);
    if (m->context)  pw_context_destroy (m->context);
    if (m->pw_loop)  pw_loop_leave (m->pw_loop);
    if (m->pw_loop_handle) pw_main_loop_destroy (m->pw_loop_handle);
    g_free (m->filter);
    g_strfreev (m->last_processes);
    g_free (m->diagnostic);
    g_free (m);
    pw_deinit ();
}
