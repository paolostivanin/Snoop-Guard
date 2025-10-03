#include "sg-state.h"

SGSharedState sg_state = { FALSE, FALSE, NULL, NULL };

void sg_state_init(void) {
    if (sg_state.webcam_proc) { g_free(sg_state.webcam_proc); sg_state.webcam_proc = NULL; }
    if (sg_state.mic_proc) { g_free(sg_state.mic_proc); sg_state.mic_proc = NULL; }
    sg_state.webcam_active = FALSE;
    sg_state.mic_active = FALSE;
}

void sg_state_set_webcam(gboolean active, const gchar *proc) {
    sg_state.webcam_active = active;
    if (sg_state.webcam_proc) { g_free(sg_state.webcam_proc); sg_state.webcam_proc = NULL; }
    if (proc && *proc) sg_state.webcam_proc = g_strdup(proc);
}

void sg_state_set_mic(gboolean active, const gchar *proc) {
    sg_state.mic_active = active;
    if (sg_state.mic_proc) { g_free(sg_state.mic_proc); sg_state.mic_proc = NULL; }
    if (proc && *proc) sg_state.mic_proc = g_strdup(proc);
}
