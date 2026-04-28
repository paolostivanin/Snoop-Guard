#pragma once
#include <glib.h>

typedef struct {
    gboolean webcam_active;
    gboolean mic_active;
    gchar *webcam_proc;
    gchar *mic_proc;
} SGSharedState;

extern SGSharedState sg_state;

void sg_state_init(void);
void sg_state_cleanup(void);

/* Returns TRUE if the active flag or proc name changed. */
gboolean sg_state_set_webcam(gboolean active, const gchar *proc);
gboolean sg_state_set_mic(gboolean active, const gchar *proc);
