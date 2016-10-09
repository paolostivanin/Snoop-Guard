#pragma once

extern struct _devs {
    char *dev_name;
    struct _devs *next;
} *head, *curr;

struct _devs *list_webcam (void);

int is_dev_being_used (int fd, const char *dev_name);

int xioctl (int fh, unsigned long request, void *arg);

void init_device (int fd, const char *dev_name);

int open_device (const char *dev_name);
