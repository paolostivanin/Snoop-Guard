#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define GENERIC_ERROR -1
#define WEBCAM_NOT_IN_USE 0
#define WEBCAM_ALREADY_IN_USE 1

static int xioctl(int fh, unsigned long request, void *arg) {
    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}


int is_dev_being_used(int fd, const char *dev_name) {
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", dev_name);
            return GENERIC_ERROR;
        } else {
            fprintf(stderr, "Webcam is being used\n");
            return WEBCAM_ALREADY_IN_USE;
        }
    } else {
        fprintf(stdout, "Webcam is not being used\n");
        return WEBCAM_NOT_IN_USE;
    }
}


static void init_device(int fd, const char *dev_name) {
    struct v4l2_capability cap;

    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", dev_name);
            exit(EXIT_FAILURE);
        } else {
            fprintf(stderr, "VIDIOC_QUERYCAP: %s\n", strerror(errno));
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", dev_name);
        exit(EXIT_FAILURE);
    }


    is_dev_being_used(fd, dev_name);
}


int open_device(const char *dev_name) {
    struct stat st;

    if (stat(dev_name, &st) == -1) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno, strerror(errno));
        return GENERIC_ERROR;
    }

    if (!S_ISCHR (st.st_mode)) {
        fprintf(stderr, "%s is no device\n", dev_name);
        return GENERIC_ERROR;
    }

    int fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

    if (fd == -1) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno));
        return GENERIC_ERROR;
    }

    return fd;
}


int main(int argc, char **argv) {
    // TODO : list dev instead of using fixed name
    // TODO: daemonize me
    const char *dev_name = "/dev/video0";

    int fd = open_device(dev_name);
    init_device(fd, dev_name);

    if (fd >= 0)
        close(fd);

    return 0;
}
