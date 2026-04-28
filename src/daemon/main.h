#pragma once
#include <glib.h>

#define DEFAULT_CHECK_INTERVAL       30
#define DEFAULT_NOTIFICATION_TIMEOUT 5
#define DEFAULT_LOG_MAX_BYTES        (256 * 1024)
#define MIN_CHECK_INTERVAL           5

#define GENERIC_ERROR -1

#define WEBCAM_NOT_IN_USE     10
#define WEBCAM_ALREADY_IN_USE 11

struct _devs {
    gchar *dev_name;
    struct _devs *next;
};

typedef struct _conf_values_t {
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
ConfigValues *load_config_file   (const gchar *override_path);

struct _devs *list_webcam (void);

/* Return TRUE if dev_name is in use; if so, *proc_name_out (caller frees) is
 * the comm of the user (or NULL on attribution failure). */
gboolean check_webcam (const gchar *dev_name, gchar **proc_name_out);

/* Mic monitor (event-driven, persistent PipeWire connection). */
typedef void (*SGMicChangedFn) (gboolean active, const gchar *proc, gpointer user_data);
gboolean mic_monitor_init (const gchar *filter, SGMicChangedFn cb, gpointer user_data);
void     mic_monitor_uninit (void);
