#include "sg-state.h"
#include "sg-logging.h"

SGSharedState sg_state = {
    .webcam_active = FALSE,
    .mic_active = FALSE,
};

static gboolean
strv_equal (gchar **a, gchar **b)
{
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    for (guint i = 0; ; i++) {
        if (!a[i] || !b[i]) return a[i] == b[i];
        if (g_strcmp0 (a[i], b[i]) != 0) return FALSE;
    }
}

static gchar *
first_process (gchar **processes)
{
    return processes && processes[0] ? g_strdup (processes[0]) : NULL;
}

void sg_state_init (void)
{
    g_clear_pointer (&sg_state.webcam_proc, g_free);
    g_clear_pointer (&sg_state.mic_proc, g_free);
    g_clear_pointer (&sg_state.webcam_processes, g_strfreev);
    g_clear_pointer (&sg_state.mic_processes, g_strfreev);
    g_clear_pointer (&sg_state.webcam_unknown_devices, g_strfreev);
    g_clear_pointer (&sg_state.webcam_health, g_free);
    g_clear_pointer (&sg_state.mic_health, g_free);
    g_clear_pointer (&sg_state.webcam_diagnostic, g_free);
    g_clear_pointer (&sg_state.mic_diagnostic, g_free);
    sg_state.webcam_active = FALSE;
    sg_state.mic_active = FALSE;
    sg_state.webcam_health = g_strdup ("ok");
    sg_state.mic_health = g_strdup ("unavailable");
}

void sg_state_cleanup (void)
{
    sg_state_init ();
}

gboolean
sg_state_set_webcam (gboolean active,
                     gchar **processes,
                     gchar **unknown_devices,
                     const gchar *health,
                     const gchar *diagnostic)
{
    gboolean changed = sg_state.webcam_active != active ||
                       !strv_equal (sg_state.webcam_processes, processes) ||
                       !strv_equal (sg_state.webcam_unknown_devices, unknown_devices) ||
                       g_strcmp0 (sg_state.webcam_health, health) != 0 ||
                       g_strcmp0 (sg_state.webcam_diagnostic, diagnostic) != 0;
    sg_state.webcam_active = active;
    g_clear_pointer (&sg_state.webcam_processes, g_strfreev);
    g_clear_pointer (&sg_state.webcam_unknown_devices, g_strfreev);
    g_clear_pointer (&sg_state.webcam_proc, g_free);
    g_free (sg_state.webcam_health);
    g_free (sg_state.webcam_diagnostic);
    sg_state.webcam_processes = g_strdupv (processes);
    sg_state.webcam_unknown_devices = g_strdupv (unknown_devices);
    sg_state.webcam_proc = first_process (processes);
    sg_state.webcam_health = g_strdup (health ? health : "unavailable");
    sg_state.webcam_diagnostic = g_strdup (diagnostic);
    if (changed) {
        gchar *joined = g_strjoinv (",", sg_state.webcam_processes);
        gchar *msg = g_strdup_printf ("webcam_active=%s health=%s processes=%s",
                                      active ? "true" : "false",
                                      sg_state.webcam_health,
                                      joined ? joined : "");
        sg_log_event (msg);
        g_free (joined);
        g_free (msg);
    }
    return changed;
}

gboolean
sg_state_set_mic (gboolean active,
                  gchar **processes,
                  const gchar *health,
                  const gchar *diagnostic)
{
    gboolean changed = sg_state.mic_active != active ||
                       !strv_equal (sg_state.mic_processes, processes) ||
                       g_strcmp0 (sg_state.mic_health, health) != 0 ||
                       g_strcmp0 (sg_state.mic_diagnostic, diagnostic) != 0;
    sg_state.mic_active = active;
    g_clear_pointer (&sg_state.mic_processes, g_strfreev);
    g_clear_pointer (&sg_state.mic_proc, g_free);
    g_free (sg_state.mic_health);
    g_free (sg_state.mic_diagnostic);
    sg_state.mic_processes = g_strdupv (processes);
    sg_state.mic_proc = first_process (processes);
    sg_state.mic_health = g_strdup (health ? health : "unavailable");
    sg_state.mic_diagnostic = g_strdup (diagnostic);
    if (changed) {
        gchar *joined = g_strjoinv (",", sg_state.mic_processes);
        gchar *msg = g_strdup_printf ("mic_active=%s health=%s processes=%s",
                                      active ? "true" : "false",
                                      sg_state.mic_health,
                                      joined ? joined : "");
        sg_log_event (msg);
        g_free (joined);
        g_free (msg);
    }
    return changed;
}
