#pragma once

#define SW_NAME "WebMonit"
#define SW_VERSION "1.0-alpha"
#define DEV_NAME "Paolo Stivanin"
#define DEV_EMAIL "info@paolostivanin.com"

#define WEBCAM 10
#define MIC 15

extern struct _devs {
    char *dev_name;
    struct _devs *next;
} *head, *curr;

struct _devs *list_webcam (void);

int get_webcam_status (int fd, const char *dev_name);

int xioctl (int fh, unsigned long request, void *arg);

void init_device (int fd, const char *dev_name);

int open_device (const char *dev_name);

int set_nonblock (int fd);

int server_mode (void);

int client_mode (void);