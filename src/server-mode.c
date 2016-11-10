#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include "webmonit.h"

#define GENERIC_ERROR -1
#define WEBCAM_NOT_IN_USE 0
#define WEBCAM_ALREADY_IN_USE 1


int server_mode (void)
{
    /* TODO:
        - get interval and ignored app from file
        - list_webcams
        - check whether webcams are in use
        - send notification
        - log somewhere
        - sleep default or as specified by interval
        - repeat
    */
    struct _devs *head, *tmp;
    int fd;

    head = list_webcam ();

    while (head) {
        fd = open_device (head->dev_name);
        init_device (fd, head->dev_name);

        if (fd >= 0)
            close (fd);

        tmp = head;
        head = head->next;
        free (tmp);
    }

    return 0;
}


int set_nonblock (int fd)
{
    int flags = fcntl (fd, F_GETFL);
    if (flags < 0)
        return flags;

    flags |= O_NONBLOCK;
    if (fcntl (fd, F_SETFL, flags) < 0)
        return -1;

    return 0;
}


int open_device (const char *dev_name)
{
    struct stat st;

    if (stat (dev_name, &st) == -1) {
        fprintf (stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno, strerror (errno));
        return GENERIC_ERROR;
    }

    if (!S_ISCHR (st.st_mode)) {
        fprintf (stderr, "%s is no device\n", dev_name);
        return GENERIC_ERROR;
    }

    int fd = open (dev_name, O_RDWR | O_NONBLOCK, 0);

    if (fd == -1) {
        fprintf (stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror (errno));
        return GENERIC_ERROR;
    }

    return fd;
}


void init_device (int fd, const char *dev_name)
{
    struct v4l2_capability cap;

    if (xioctl (fd, VIDIOC_QUERYCAP, &cap) == -1) {
        if (errno == EINVAL) {
            fprintf (stderr, "%s is no V4L2 device\n", dev_name);
            exit (EXIT_FAILURE);
        } else {
            fprintf (stderr, "VIDIOC_QUERYCAP: %s\n", strerror (errno));
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf (stderr, "%s is no video capture device\n", dev_name);
        exit (EXIT_FAILURE);
    }


    get_webcam_status (fd, dev_name);
}


int xioctl (int fh, unsigned long request, void *arg)
{
    int r;

    do {
        r = ioctl (fh, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}


int get_webcam_status (int fd, const char *dev_name)
{
    struct v4l2_requestbuffers req;

    memset (&req, 0, sizeof (req));

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl (fd, VIDIOC_REQBUFS, &req) == -1) {
        if (errno == EINVAL) {
            fprintf (stderr, "%s does not support memory mapping\n", dev_name);
            return GENERIC_ERROR;
        } else {
            fprintf (stderr, "Webcam is being used\n");
            return WEBCAM_ALREADY_IN_USE;
        }
    } else {
        fprintf (stdout, "Webcam is not being used\n");
        return WEBCAM_NOT_IN_USE;
    }
}