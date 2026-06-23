#pragma once
#include <glib.h>

#define DEFAULT_CHECK_INTERVAL       2
#define DEFAULT_NOTIFICATION_TIMEOUT 5
#define DEFAULT_LOG_MAX_BYTES        ((gsize) 256 * 1024)
#define MIN_CHECK_INTERVAL           1
#define MAX_CHECK_INTERVAL           3600
#define MAX_NOTIFICATION_TIMEOUT     86400
#define MAX_LOG_BYTES                (1024UL * 1024UL * 1024UL)

#define WEBCAM_NOT_IN_USE     10
#define WEBCAM_ALREADY_IN_USE 11

struct SGDevice {
    gchar *dev_name;
    struct SGDevice *next;
};

typedef struct ConfigValues {
    guint64  check_interval;
    gint     notification_timeout;     /* seconds; 0 = manual dismissal */
    gsize    log_max_bytes;
    gchar   *microphone_device;        /* may be NULL */
    gchar  **allow_list;               /* webcam */
    gchar  **deny_list;                /* webcam */
    gchar  **mic_allow_list;
    gchar  **mic_deny_list;
} ConfigValues;

void          config_values_free (ConfigValues *cv);
ConfigValues *load_config_file   (const gchar *override_path,
                                  gboolean explicit_path,
                                  GError **error);

struct SGDevice *list_webcam (void);
void             free_webcam_list (struct SGDevice *head);

typedef enum {
    SG_MONITOR_OK,
    SG_MONITOR_DEGRADED,
    SG_MONITOR_UNAVAILABLE,
} SGMonitorHealth;

const gchar *sg_monitor_health_to_string (SGMonitorHealth health);

typedef struct {
    gboolean active;
    gchar **processes;
    gchar **unknown_devices;
    SGMonitorHealth health;
    gchar *diagnostic;
} SGMonitorSnapshot;

void     sg_monitor_snapshot_clear (SGMonitorSnapshot *snapshot);
gboolean check_webcams (SGMonitorSnapshot *snapshot);

/* Mic monitor (event-driven, persistent PipeWire connection). */
typedef void (*SGMicChangedFn) (const SGMonitorSnapshot *snapshot, gpointer user_data);
gboolean mic_monitor_init (const gchar *filter, SGMicChangedFn cb, gpointer user_data);
void     mic_monitor_uninit (void);
