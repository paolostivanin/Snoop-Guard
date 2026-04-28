#pragma once
#include <glib.h>
#include <gio/gio.h>

/* GDBus service for SnoopGuard daemon
 *   Bus name: org.snoopguard.Service
 *   Path:     /org/snoopguard/Service
 *   Interface: org.snoopguard.Service
 * Methods:
 *   GetStatus() -> (b b s s) webcam_active, mic_active, webcam_proc, mic_proc
 *   GetRecentEvents(i max_lines) -> (as) lines
 *   ReloadConfig() -> ()  (asks the daemon to reload its config)
 * Signals:
 *   ActivityChanged(b b s s)
 */

typedef struct {
    gboolean webcam_active;
    gboolean mic_active;
    gchar *webcam_proc;
    gchar *mic_proc;
} SGStatus;

typedef void (*SGReloadFn) (gpointer user_data);

void sg_dbus_init (SGReloadFn reload_cb, gpointer reload_data);
void sg_dbus_uninit (void);

/* Update cached status and emit ActivityChanged only if it differs. */
void sg_dbus_update_status (const SGStatus *status);

/* Borrowed bus connection (NULL if not yet acquired). */
GDBusConnection *sg_dbus_get_bus (void);
