#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include "main.h"

static gint xioctl (gint fh, gulong request, void *arg);
static gint open_device (const gchar *dev_name);
static gint init_device (gint fd, const gchar *dev_name);
static gint probe_in_use (gint fd, const gchar *dev_name);
static gchar *find_proc_using_webcam (const gchar *webcam_dev);
static gchar *extract_proc_name_from_stat (const gchar *stat_contents);

static gint
xioctl (gint fh, gulong request, void *arg)
{
    gint r;
    do {
        r = ioctl (fh, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static gint
open_device (const gchar *dev_name)
{
    if (!dev_name) return GENERIC_ERROR;
    int fd = open (dev_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd == -1) {
        /* EBUSY or EACCES are common; don't spam the journal at info-level. */
        return GENERIC_ERROR;
    }
    struct stat st;
    if (fstat (fd, &st) == -1 || !S_ISCHR (st.st_mode)) {
        (void) close (fd);
        return GENERIC_ERROR;
    }
    return fd;
}

static gint
init_device (gint fd, const gchar *dev_name)
{
    struct v4l2_capability cap;
    memset (&cap, 0, sizeof (cap));
    if (xioctl (fd, VIDIOC_QUERYCAP, &cap) == -1) {
        if (errno != EINVAL) {
            g_message ("VIDIOC_QUERYCAP on %s failed: %s", dev_name, g_strerror (errno));
        }
        return GENERIC_ERROR;
    }
    guint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) && !(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        return GENERIC_ERROR;
    }
    return probe_in_use (fd, dev_name);
}

/* Probe whether the device has an active streaming user. Avoids any allocation
 * that could trigger device LEDs by using an empty REQBUFS as a no-op probe;
 * if a streamer is active, REQBUFS fails with EBUSY. */
static gint
probe_in_use (gint fd, const gchar *dev_name)
{
    struct v4l2_requestbuffers req;
    memset (&req, 0, sizeof (req));
    req.count  = 0; /* empty: pure probe, releases any allocations */
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl (fd, VIDIOC_REQBUFS, &req) == -1) {
        if (errno == EBUSY) return WEBCAM_ALREADY_IN_USE;
        if (errno == EINVAL) {
            /* Try MPLANE as a fallback. */
            memset (&req, 0, sizeof (req));
            req.count  = 0;
            req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            req.memory = V4L2_MEMORY_MMAP;
            if (xioctl (fd, VIDIOC_REQBUFS, &req) == -1) {
                if (errno == EBUSY) return WEBCAM_ALREADY_IN_USE;
                /* Driver does not support memory mapping for this buf type. */
                g_message ("%s does not support memory mapping", dev_name);
            } else {
                return WEBCAM_NOT_IN_USE;
            }
            return GENERIC_ERROR;
        }
        return GENERIC_ERROR;
    }
    return WEBCAM_NOT_IN_USE;
}

/* Iterate all numeric entries under /proc and find the first PID whose fd
 * directory contains a symlink to webcam_dev. Returns the comm of that
 * process (caller frees) or NULL. */
static gchar *
find_proc_using_webcam (const gchar *webcam_dev)
{
    GDir *proc = g_dir_open ("/proc", 0, NULL);
    if (!proc) return NULL;
    gchar *result = NULL;
    const gchar *entry;
    while ((entry = g_dir_read_name (proc)) != NULL) {
        if (entry[0] < '0' || entry[0] > '9') continue;
        gboolean numeric = TRUE;
        for (const gchar *p = entry; *p; p++) {
            if (*p < '0' || *p > '9') { numeric = FALSE; break; }
        }
        if (!numeric) continue;

        gchar *fd_dir = g_strconcat ("/proc/", entry, "/fd", NULL);
        GDir *fdd = g_dir_open (fd_dir, 0, NULL);
        if (!fdd) {
            /* most likely EACCES for other users' processes */
            g_free (fd_dir);
            continue;
        }
        gboolean match = FALSE;
        const gchar *f;
        while ((f = g_dir_read_name (fdd)) != NULL) {
            gchar *full = g_strconcat (fd_dir, "/", f, NULL);
            char target[PATH_MAX];
            ssize_t n = readlink (full, target, sizeof (target) - 1);
            g_free (full);
            if (n <= 0) continue;
            target[n] = '\0';
            if (strcmp (target, webcam_dev) == 0) {
                match = TRUE;
                break;
            }
        }
        g_dir_close (fdd);
        g_free (fd_dir);

        if (match) {
            gchar *stat_path = g_strconcat ("/proc/", entry, "/stat", NULL);
            gchar *contents = NULL;
            if (g_file_get_contents (stat_path, &contents, NULL, NULL)) {
                result = extract_proc_name_from_stat (contents);
                g_free (contents);
            }
            g_free (stat_path);
            break;
        }
    }
    g_dir_close (proc);
    return result;
}

static gchar *
extract_proc_name_from_stat (const gchar *stat_contents)
{
    if (!stat_contents) return NULL;
    const gchar *start = strchr (stat_contents, '(');
    const gchar *end   = strrchr (stat_contents, ')');
    if (!start || !end || end <= start + 1) return NULL;
    gsize len = (gsize) (end - start - 1);
    if (len > 64) len = 64; /* sanity cap */
    return g_strndup (start + 1, len);
}

/* Returns TRUE if the webcam at dev_name is in use. If so and proc_name_out is
 * non-NULL, it is filled with a newly-allocated comm string (or NULL on
 * attribution failure; caller frees). */
gboolean
check_webcam (const gchar *dev_name, gchar **proc_name_out)
{
    if (proc_name_out) *proc_name_out = NULL;
    gint fd = open_device (dev_name);
    if (fd < 0) return FALSE;
    gint status = init_device (fd, dev_name);
    (void) close (fd);
    if (status != WEBCAM_ALREADY_IN_USE) return FALSE;
    if (proc_name_out) *proc_name_out = find_proc_using_webcam (dev_name);
    return TRUE;
}
