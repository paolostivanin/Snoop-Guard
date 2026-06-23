#pragma once
#include <glib.h>

typedef struct {
    gboolean webcam_active;
    gboolean mic_active;
    gchar *webcam_proc;
    gchar *mic_proc;
    gchar **webcam_processes;
    gchar **mic_processes;
    gchar **webcam_unknown_devices;
    gchar *webcam_health;
    gchar *mic_health;
    gchar *webcam_diagnostic;
    gchar *mic_diagnostic;
} SGSharedState;

extern SGSharedState sg_state;

void sg_state_init(void);
void sg_state_cleanup(void);

/* Returns TRUE if the active flag or proc name changed. */
gboolean sg_state_set_webcam (gboolean active,
                              gchar **processes,
                              gchar **unknown_devices,
                              const gchar *health,
                              const gchar *diagnostic);
gboolean sg_state_set_mic (gboolean active,
                           gchar **processes,
                           const gchar *health,
                           const gchar *diagnostic);
