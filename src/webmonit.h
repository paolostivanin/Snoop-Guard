#pragma once

#define SW_NAME "WebMonit"
#define SW_VERSION "1.0-alpha"
#define DEV_NAME "Paolo Stivanin"
#define DEV_EMAIL "info@paolostivanin.com"

#define DEFAULT_CHECK_INTERVAL 30
#define DEFAULT_NOTIFICATION_TIMEOUT 5
#define DEFAULT_MIC_NAME "sysdefault"

struct _devs {
    char *dev_name;
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

int get_webcam_status (int fd, const char *dev_name);

int get_mic_status (const char *mic);

int xioctl (int fh, unsigned long request, void *arg);

void init_device (int fd, const char *dev_name);

int open_device (const char *dev_name);

int set_nonblock (int fd);

int server_mode (void);

int client_mode (void);