#include <glib.h>
#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>
#include <string.h>
#include "main.h"
#include "sg-state.h"

typedef struct {
    struct pw_node *node;
    struct spa_hook listener;
} PwNodeProxy;

typedef struct {
    const gchar *filter;
    gboolean active;
    gchar *proc_name;
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook registry_listener;
    struct spa_hook core_listener;
    GPtrArray *nodes;
    int sync_seq;
} PwQuery;

static gboolean contains_case_insensitive(const gchar *text, const gchar *needle)
{
    if (!text || !needle || !*needle) return FALSE;
    gchar *text_l = g_ascii_strdown(text, -1);
    gchar *needle_l = g_ascii_strdown(needle, -1);
    gboolean match = g_strrstr(text_l, needle_l) != NULL;
    g_free(text_l);
    g_free(needle_l);
    return match;
}

static gboolean is_capture_node(const gchar *media_class, enum pw_node_state state)
{
    if (!media_class) return FALSE;
    if (state != PW_NODE_STATE_RUNNING) return FALSE;
    return g_strrstr(media_class, "Audio/Source") != NULL ||
           g_strrstr(media_class, "Stream/Input/Audio") != NULL;
}

static gboolean node_matches_filter(const gchar *node_name, const gchar *app_name, const gchar *filter)
{
    if (!filter || !*filter) return TRUE;
    if (contains_case_insensitive(node_name, filter)) return TRUE;
    if (contains_case_insensitive(app_name, filter)) return TRUE;
    return FALSE;
}

static gchar *pick_proc_name(const gchar *app_binary, const gchar *app_name, const gchar *node_name)
{
    if (app_binary && *app_binary) return g_strdup(app_binary);
    if (app_name && *app_name) return g_strdup(app_name);
    if (node_name && *node_name) return g_strdup(node_name);
    return NULL;
}

static void on_node_info(void *data, const struct pw_node_info *info)
{
    PwQuery *query = data;
    if (!info || query->active || !info->props) return;

    const gchar *media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
    if (!is_capture_node(media_class, info->state)) return;

    const gchar *node_name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
    const gchar *app_name = spa_dict_lookup(info->props, PW_KEY_APP_NAME);
    const gchar *app_binary = spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_BINARY);

    if (!node_matches_filter(node_name, app_name, query->filter)) return;

    query->active = TRUE;
    query->proc_name = pick_proc_name(app_binary, app_name, node_name);
    pw_main_loop_quit(query->loop);
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = on_node_info,
};

static void on_registry_global(void *data, uint32_t id, uint32_t permissions,
                               const char *type, uint32_t version,
                               const struct spa_dict *props)
{
    PwQuery *query = data;
    (void)permissions;
    (void)props;

    if (!type || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;

    PwNodeProxy *node = g_new0(PwNodeProxy, 1);
    node->node = pw_registry_bind(query->registry, id, type, version, 0);
    if (!node->node) {
        g_free(node);
        return;
    }
    pw_node_add_listener(node->node, &node->listener, &node_events, query);
    g_ptr_array_add(query->nodes, node);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
};

static void on_core_done(void *data, uint32_t id, int seq)
{
    PwQuery *query = data;
    if (id == PW_ID_CORE && seq == query->sync_seq) {
        pw_main_loop_quit(query->loop);
    }
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
};

static gboolean any_pipewire_capture_active(const gchar *filter, gchar **proc_name_out)
{
    PwQuery query = {
        .filter = filter,
        .active = FALSE,
        .proc_name = NULL,
        .loop = NULL,
        .context = NULL,
        .core = NULL,
        .registry = NULL,
        .nodes = NULL,
        .sync_seq = -1,
    };

    pw_init(NULL, NULL);
    query.loop = pw_main_loop_new(NULL);
    if (!query.loop) {
        g_printerr("Failed to create PipeWire main loop\n");
        pw_deinit();
        return FALSE;
    }
    query.context = pw_context_new(pw_main_loop_get_loop(query.loop), NULL, 0);
    if (!query.context) {
        g_printerr("Failed to create PipeWire context\n");
        pw_main_loop_destroy(query.loop);
        pw_deinit();
        return FALSE;
    }
    query.core = pw_context_connect(query.context, NULL, 0);
    if (!query.core) {
        g_printerr("Failed to connect to PipeWire core\n");
        pw_context_destroy(query.context);
        pw_main_loop_destroy(query.loop);
        pw_deinit();
        return FALSE;
    }

    query.registry = pw_core_get_registry(query.core, PW_VERSION_REGISTRY, 0);
    if (!query.registry) {
        g_printerr("Failed to get PipeWire registry\n");
        pw_core_disconnect(query.core);
        pw_context_destroy(query.context);
        pw_main_loop_destroy(query.loop);
        pw_deinit();
        return FALSE;
    }

    query.nodes = g_ptr_array_new();
    pw_registry_add_listener(query.registry, &query.registry_listener, &registry_events, &query);
    pw_core_add_listener(query.core, &query.core_listener, &core_events, &query);
    query.sync_seq = pw_core_sync(query.core, PW_ID_CORE, 0);

    pw_main_loop_run(query.loop);

    if (proc_name_out) {
        *proc_name_out = query.proc_name ? g_strdup(query.proc_name) : NULL;
    }

    for (guint i = 0; i < query.nodes->len; i++) {
        PwNodeProxy *node = g_ptr_array_index(query.nodes, i);
        if (node->node) pw_proxy_destroy((struct pw_proxy *)node->node);
        g_free(node);
    }
    g_ptr_array_free(query.nodes, TRUE);

    if (query.registry) pw_proxy_destroy((struct pw_proxy *)query.registry);
    if (query.core) pw_core_disconnect(query.core);
    if (query.context) pw_context_destroy(query.context);
    if (query.loop) pw_main_loop_destroy(query.loop);
    g_clear_pointer(&query.proc_name, g_free);
    pw_deinit();

    return query.active;
}

int get_mic_status (const gchar *mic)
{
    gchar *proc = NULL;
    gboolean active = any_pipewire_capture_active(mic, &proc);
    sg_state_set_mic(active, proc);
    if (proc) g_free(proc);
    if (active) {
        g_print("Mic IS being used\n");
        return MIC_ALREADY_IN_USE;
    }
    g_print("Mic is NOT being used\n");
    return MIC_NOT_IN_USE;
}
