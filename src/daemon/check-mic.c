#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include "main.h"
#include "sg-state.h"

typedef struct {
    gchar *node_name;
    gchar *media_class;
    gchar *state;
    gchar *app_name;
    gchar *app_binary;
} PwNodeInfo;

static gchar *extract_quoted_value(const gchar *line)
{
    if (!line) return NULL;
    const gchar *start = strchr(line, '"');
    const gchar *end = strrchr(line, '"');
    if (!start || !end || end <= start) return NULL;
    return g_strndup(start + 1, (gsize)(end - start - 1));
}

static gboolean is_capture_node(const PwNodeInfo *node)
{
    if (!node->media_class || !node->state) return FALSE;
    if (g_strcmp0(node->state, "running") != 0) return FALSE;
    return g_strrstr(node->media_class, "Audio/Source") != NULL ||
           g_strrstr(node->media_class, "Stream/Input/Audio") != NULL;
}

static gboolean node_matches_filter(const PwNodeInfo *node, const gchar *filter)
{
    if (!filter || !*filter) return TRUE;
    gchar *filter_l = g_ascii_strdown(filter, -1);
    gboolean match = FALSE;
    if (node->node_name) {
        gchar *name_l = g_ascii_strdown(node->node_name, -1);
        match = g_strrstr(name_l, filter_l) != NULL;
        g_free(name_l);
    }
    if (!match && node->app_name) {
        gchar *app_l = g_ascii_strdown(node->app_name, -1);
        match = g_strrstr(app_l, filter_l) != NULL;
        g_free(app_l);
    }
    g_free(filter_l);
    return match;
}

static gchar *pick_proc_name(const PwNodeInfo *node)
{
    if (node->app_binary && *node->app_binary) return g_strdup(node->app_binary);
    if (node->app_name && *node->app_name) return g_strdup(node->app_name);
    if (node->node_name && *node->node_name) return g_strdup(node->node_name);
    return NULL;
}

static gboolean parse_pw_cli_output(const gchar *output, const gchar *filter, gchar **proc_name_out)
{
    if (!output) return FALSE;
    gchar **lines = g_strsplit(output, "\n", -1);
    PwNodeInfo node = {0};
    gboolean active = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (g_str_has_prefix(line, "id ")) {
            if (is_capture_node(&node) && node_matches_filter(&node, filter)) {
                active = TRUE;
                if (proc_name_out) *proc_name_out = pick_proc_name(&node);
                break;
            }
            g_clear_pointer(&node.node_name, g_free);
            g_clear_pointer(&node.media_class, g_free);
            g_clear_pointer(&node.state, g_free);
            g_clear_pointer(&node.app_name, g_free);
            g_clear_pointer(&node.app_binary, g_free);
            continue;
        }
        if (g_str_has_prefix(line, "node.name =")) {
            g_clear_pointer(&node.node_name, g_free);
            node.node_name = extract_quoted_value(line);
            continue;
        }
        if (g_str_has_prefix(line, "media.class =")) {
            g_clear_pointer(&node.media_class, g_free);
            node.media_class = extract_quoted_value(line);
            continue;
        }
        if (g_str_has_prefix(line, "node.state =")) {
            g_clear_pointer(&node.state, g_free);
            node.state = extract_quoted_value(line);
            continue;
        }
        if (g_str_has_prefix(line, "application.name =")) {
            g_clear_pointer(&node.app_name, g_free);
            node.app_name = extract_quoted_value(line);
            continue;
        }
        if (g_str_has_prefix(line, "application.process.binary =")) {
            g_clear_pointer(&node.app_binary, g_free);
            node.app_binary = extract_quoted_value(line);
            continue;
        }
    }

    if (!active && is_capture_node(&node) && node_matches_filter(&node, filter)) {
        active = TRUE;
        if (proc_name_out) *proc_name_out = pick_proc_name(&node);
    }

    g_clear_pointer(&node.node_name, g_free);
    g_clear_pointer(&node.media_class, g_free);
    g_clear_pointer(&node.state, g_free);
    g_clear_pointer(&node.app_name, g_free);
    g_clear_pointer(&node.app_binary, g_free);
    g_strfreev(lines);
    return active;
}

static gboolean any_pipewire_capture_active(const gchar *filter, gchar **proc_name_out)
{
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    gint exit_status = 0;
    GError *err = NULL;

    if (!g_spawn_command_line_sync("pw-cli ls Node", &stdout_buf, &stderr_buf, &exit_status, &err)) {
        g_printerr("Failed to run pw-cli: %s\n", err ? err->message : "unknown error");
        g_clear_error(&err);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }
    if (exit_status != 0) {
        if (stderr_buf && *stderr_buf) g_printerr("%s\n", stderr_buf);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }

    gboolean active = parse_pw_cli_output(stdout_buf, filter, proc_name_out);
    g_free(stdout_buf);
    g_free(stderr_buf);
    return active;
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
