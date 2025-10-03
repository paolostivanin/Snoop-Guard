#pragma once
#include <glib.h>
#include <gio/gio.h>

// Simple GDBus service for SnoopGuard daemon
// Bus name: org.snoopguard.Service
// Object path: /org/snoopguard/Service
// Interface: org.snoopguard.Service
// Methods:
//   GetStatus() -> (b b s s) webcam_active, mic_active, webcam_proc, mic_proc
//   GetRecentEvents(i max_lines) -> (as) lines
// Signals:
//   ActivityChanged(b b s s)

typedef struct {
    gboolean webcam_active;
    gboolean mic_active;
    gchar *webcam_proc;
    gchar *mic_proc;
} SGStatus;

void sg_dbus_init(GMainLoop *loop);
void sg_dbus_update_status(const SGStatus *status);
