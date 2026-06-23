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
    gchar **webcam_processes;
    gchar **mic_processes;
    gchar **webcam_unknown_devices;
    gchar *webcam_health;
    gchar *mic_health;
    gchar *webcam_diagnostic;
    gchar *mic_diagnostic;
} SGStatus;

typedef gboolean (*SGReloadFn) (gpointer user_data, GError **error);

void sg_dbus_init (SGReloadFn reload_cb, gpointer reload_data);
void sg_dbus_uninit (void);

/* Update cached status and emit ActivityChanged only if it differs. */
void sg_dbus_update_status (const SGStatus *status);

/* Borrowed bus connection (NULL if not yet acquired). */
GDBusConnection *sg_dbus_get_bus (void);

/* TRUE after a bus connection/name ownership failure that should make the
 * daemon exit unsuccessfully so its service manager can restart it. */
gboolean sg_dbus_had_fatal_error (void);
