#include "sg-state.h"
#include "sg-logging.h"

SGSharedState sg_state = { FALSE, FALSE, NULL, NULL };

void sg_state_init(void) {
    if (sg_state.webcam_proc) { g_free(sg_state.webcam_proc); sg_state.webcam_proc = NULL; }
    if (sg_state.mic_proc) { g_free(sg_state.mic_proc); sg_state.mic_proc = NULL; }
    sg_state.webcam_active = FALSE;
    sg_state.mic_active = FALSE;
}

void sg_state_set_webcam(gboolean active, const gchar *proc) {
    gboolean changed = (sg_state.webcam_active != active) ||
                       (g_strcmp0(sg_state.webcam_proc, proc) != 0);
    sg_state.webcam_active = active;
    if (sg_state.webcam_proc) { g_free(sg_state.webcam_proc); sg_state.webcam_proc = NULL; }
    if (proc && *proc) sg_state.webcam_proc = g_strdup(proc);
    if (changed) {
        gchar *msg = g_strdup_printf("webcam_active=%s proc=%s",
                                     active ? "true" : "false",
                                     proc && *proc ? proc : "");
        sg_log_event(msg);
        g_free(msg);
    }
}

void sg_state_set_mic(gboolean active, const gchar *proc) {
    gboolean changed = (sg_state.mic_active != active) ||
                       (g_strcmp0(sg_state.mic_proc, proc) != 0);
    sg_state.mic_active = active;
    if (sg_state.mic_proc) { g_free(sg_state.mic_proc); sg_state.mic_proc = NULL; }
    if (proc && *proc) sg_state.mic_proc = g_strdup(proc);
    if (changed) {
        gchar *msg = g_strdup_printf("mic_active=%s proc=%s",
                                     active ? "true" : "false",
                                     proc && *proc ? proc : "");
        sg_log_event(msg);
        g_free(msg);
    }
}
