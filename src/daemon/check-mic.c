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
    gchar               *last_proc;
} MicMonitor;

static MicMonitor *g_monitor = NULL;

/* ---------- helpers ---------- */

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

static gboolean
is_capture_class (const gchar *media_class)
{
    if (!media_class) return FALSE;
    return strstr (media_class, "Audio/Source") != NULL ||
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
    const gchar *proc_name = NULL;
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init (&it, m->nodes);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        PwNode *n = value;
        if (n->active) {
            any_active = TRUE;
            /* Pick the first; could pick the most recently changed. */
            if (!proc_name) proc_name = n->app_binary ? n->app_binary :
                                        n->app_name   ? n->app_name   :
                                        n->node_name;
            break;
        }
    }
    gboolean changed = (m->last_active != any_active) ||
                       (g_strcmp0 (m->last_proc, proc_name) != 0);
    if (!changed) return;
    m->last_active = any_active;
    g_free (m->last_proc);
    m->last_proc = proc_name ? g_strdup (proc_name) : NULL;
    if (m->cb) m->cb (any_active, proc_name, m->cb_data);
}

static void
update_from_props (PwNode *n, const struct spa_dict *props)
{
    if (!props) return;
    const gchar *mc = spa_dict_lookup (props, PW_KEY_MEDIA_CLASS);
    const gchar *nn = spa_dict_lookup (props, PW_KEY_NODE_NAME);
    const gchar *an = spa_dict_lookup (props, PW_KEY_APP_NAME);
    const gchar *ab = spa_dict_lookup (props, PW_KEY_APP_PROCESS_BINARY);
    if (mc) { g_free (n->media_class); n->media_class = g_strdup (mc); }
    if (nn) { g_free (n->node_name);   n->node_name   = g_strdup (nn); }
    if (an) { g_free (n->app_name);    n->app_name    = g_strdup (an); }
    if (ab) { g_free (n->app_binary);  n->app_binary  = g_strdup (ab); }
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

/* ---------- GLib integration ---------- */

static gboolean
on_pw_fd_ready (gint fd, GIOCondition cond, gpointer data)
{
    (void) fd;
    (void) cond;
    MicMonitor *m = data;
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
    m->last_proc   = NULL;

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

    m->registry = pw_core_get_registry (m->core, PW_VERSION_REGISTRY, 0);
    if (!m->registry) goto fail;
    pw_registry_add_listener (m->registry, &m->registry_listener,
                              &registry_events, m);

    int fd = pw_loop_get_fd (m->pw_loop);
    if (fd < 0) goto fail;
    m->pw_source_id = g_unix_fd_add (fd, G_IO_IN, on_pw_fd_ready, m);

    /* Claim the loop for this thread (the main thread). */
    pw_loop_enter (m->pw_loop);

    g_monitor = m;
    return TRUE;

fail:
    if (m->registry) pw_proxy_destroy ((struct pw_proxy *) m->registry);
    if (m->core)     pw_core_disconnect (m->core);
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
    if (m->core)     pw_core_disconnect (m->core);
    if (m->context)  pw_context_destroy (m->context);
    if (m->pw_loop)  pw_loop_leave (m->pw_loop);
    if (m->pw_loop_handle) pw_main_loop_destroy (m->pw_loop_handle);
    g_free (m->filter);
    g_free (m->last_proc);
    g_free (m);
    pw_deinit ();
}
