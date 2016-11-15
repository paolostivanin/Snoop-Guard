#pragma once

#define SW_NAME "WebMonit"
#define SW_VERSION "1.0-alpha"
#define DEV_NAME "Paolo Stivanin"
#define DEV_EMAIL "info@paolostivanin.com"

#define CONFIG_FILE_NAME "webmonit.ini"

#define DEFAULT_CHECK_INTERVAL 30
#define DEFAULT_NOTIFICATION_TIMEOUT 5
#define DEFAULT_MIC_NAME "sysdefault"

struct _devs {
    gchar *dev_name;
    struct _devs *next;
} *head, *curr;

typedef struct _conf_values_t {
    gint check_interval;
    gint notification_timeout;
    gchar *microphone_device;
    gchar **ignore_apps;
} ConfigValues;

struct _devs *list_webcam (void);

ConfigValues *load_config_file (void);

gint get_webcam_status (gint fd, const gchar *dev_name);

gint get_mic_status (const gchar *mic);

gint xioctl (gint fh, gulong request, void *arg);

void init_device (gint fd, const gchar *dev_name);

gint open_device (const gchar *dev_name);

gint set_nonblock (gint fd);

gint server_mode (void);

gint client_mode (void);