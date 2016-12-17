#pragma once

#define SW_NAME "WebMonit"
#define SW_VERSION "1.0-alpha"
#define DEV_NAME "Paolo Stivanin"
#define DEV_EMAIL "info@paolostivanin.com"

#define CONFIG_FILE_NAME "snoop-guard.ini"

#define DEFAULT_CHECK_WEBCAM_INTERVAL 30
#define DEFAULT_CHECK_MIC_INTERVAL 30
#define DEFAULT_NOTIFICATION_TIMEOUT 5
#define DEFAULT_MIC_NAME "sysdefault"

#define GENERIC_ERROR -1
#define WEBCAM_NOT_IN_USE 10
#define WEBCAM_ALREADY_IN_USE 11
#define WEBCAM_USED_BY_IGNORED_APP 12

#define MIC_NOT_IN_USE 20
#define MIC_ALREADY_IN_USE 21

struct _devs {
    gchar *dev_name;
    struct _devs *next;
} *head, *curr;

typedef struct _conf_values_t {
    gint check_webcam_interval;
    gint check_mic_interval;
    gint notification_timeout;
    gchar *microphone_device;
    gchar **ignore_apps;
} ConfigValues;

struct _devs *list_webcam (void);

ConfigValues *load_config_file (void);

gint get_mic_status (const gchar *mic);

void check_webcam (const gchar *dev_name, gchar **ignore_apps);

guint get_ppid_from_pname (const gchar *pname);