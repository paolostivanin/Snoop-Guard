#include <glib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <alsa/asoundlib.h>
#include "../common/webmonit.h"

#define GENERIC_ERROR -1
#define WEBCAM_NOT_IN_USE 10
#define WEBCAM_ALREADY_IN_USE 11
#define MIC_NOT_IN_USE 20
#define MIC_ALREADY_IN_USE 21


gint
main (gint argc, gchar **argv)
{
    /* TODO:
        - get interval and ignored app from file
        - check whether webcam are being used
        - check whether mic is being used
        - send notification
        - log somewhere
        - how to treat ignored apps?
        - sleep default or as specified by interval
    */
    struct _devs *head, *tmp;
    gint fd;

    ConfigValues *cfg_values = load_config_file ();

    head = list_webcam ();
    // TODO: readlink ("/proc/FPID/fd/FDNUM", buf, buflen). If buf == /dev/videoX then do something
    // get_ppid_from_pname. Then readlink. If webcam is listed and app is ignored then ignore.

    while (head) {
        fd = open_device (head->dev_name);
        init_device (fd, head->dev_name);

        if (fd >= 0)
            close (fd);

        tmp = head;
        head = head->next;
        free (tmp);
    }

    // TODO mic use sysdefault name or read from config file OR SKIP IT IF mic_name is NULL
    get_mic_status ("sysdefault");

    if (cfg_values->microphone_device != NULL)
        g_free (cfg_values->microphone_device);

    if (cfg_values->ignore_apps != NULL);
        g_strfreev (cfg_values->ignore_apps);

    g_free (cfg_values);

    return 0;
}


gint
open_device (const gchar *dev_name)
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


void
init_device (gint fd, const gchar *dev_name)
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


gint
xioctl (gint fh, gulong request, void *arg)
{
    gint r;

    do {
        r = ioctl (fh, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}


gint
get_webcam_status (gint fd, const gchar *dev_name)
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


gint
get_mic_status (const gchar *mic)
{
    // TODO: grep capture /proc/asound/devices. If 1 then sysdefault? And if 2 what? DEAL WITH MULTIPLE MIC
    /* TODO arecord -L | grep -w sysdefault:CARD with system() could be a solution. Check the fork() thing to add more security
     * (https://www.securecoding.cert.org/confluence/pages/viewpage.action?pageId=2130132)
     */
    snd_pcm_t *capture_handle = NULL;

    if (snd_pcm_open (&capture_handle, mic, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        // TODO This fails also if the device name is not correct. Device presence should be checked
        return MIC_ALREADY_IN_USE;
    }
    else {
        snd_pcm_close (capture_handle);
        return MIC_NOT_IN_USE;
    }
}