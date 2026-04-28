#include "sg-state.h"
#include "sg-logging.h"

SGSharedState sg_state = { FALSE, FALSE, NULL, NULL };

void sg_state_init (void)
{
    g_clear_pointer (&sg_state.webcam_proc, g_free);
    g_clear_pointer (&sg_state.mic_proc, g_free);
    sg_state.webcam_active = FALSE;
    sg_state.mic_active = FALSE;
}

void sg_state_cleanup (void)
{
    sg_state_init ();
}

static gboolean
update_field (gboolean *flag, gchar **proc_field, gboolean active, const gchar *proc)
{
    const gchar *new_proc = (proc && *proc) ? proc : NULL;
    gboolean changed = (*flag != active) || (g_strcmp0 (*proc_field, new_proc) != 0);
    *flag = active;
    g_clear_pointer (proc_field, g_free);
    if (new_proc) *proc_field = g_strdup (new_proc);
    return changed;
}

gboolean
sg_state_set_webcam (gboolean active, const gchar *proc)
{
    gboolean changed = update_field (&sg_state.webcam_active, &sg_state.webcam_proc, active, proc);
    if (changed) {
        gchar *msg = g_strdup_printf ("webcam_active=%s proc=%s",
                                      active ? "true" : "false",
                                      sg_state.webcam_proc ? sg_state.webcam_proc : "");
        sg_log_event (msg);
        g_free (msg);
    }
    return changed;
}

gboolean
sg_state_set_mic (gboolean active, const gchar *proc)
{
    gboolean changed = update_field (&sg_state.mic_active, &sg_state.mic_proc, active, proc);
    if (changed) {
        gchar *msg = g_strdup_printf ("mic_active=%s proc=%s",
                                      active ? "true" : "false",
                                      sg_state.mic_proc ? sg_state.mic_proc : "");
        sg_log_event (msg);
        g_free (msg);
    }
    return changed;
}
