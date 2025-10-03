#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <gio/gio.h>
#include "sg-notification.h"
#include "main.h"
#include "sg-state.h"


gint open_device (const gchar *dev_name);

gint xioctl (gint fh, gulong request, void *arg);

gint init_device (gint fd, const gchar *dev_name);

gint get_webcam_status (gint fd, const gchar *dev_name);

gboolean get_webcam_from_open_fd (const gchar *dev_name, guint pid, gboolean called_from_get_proc);

gchar *get_proc_using_webcam (const gchar *webcam_dev);


static gboolean strv_contains(gchar **list, const gchar *name)
{
    if (!list || !name) return FALSE;
    for (gchar **p = list; *p; ++p) {
        if (g_strcmp0(*p, name) == 0) return TRUE;
    }
    return FALSE;
}

void
check_webcam (gint nss, const gchar *dev_name, gchar **allow_list, gchar **deny_list)
{
    gchar *message;
    gint fd = open_device (dev_name);
    if (fd >= 0) {
        gint status = init_device (fd, dev_name);
        if (status == WEBCAM_ALREADY_IN_USE) {
            gchar *proc_name = get_proc_using_webcam (dev_name);
            sg_state_set_webcam(TRUE, proc_name);
            gboolean allowed = strv_contains(allow_list, proc_name);
            gboolean denied = strv_contains(deny_list, proc_name);
            if (proc_name != NULL) {
                message = g_strconcat (proc_name, " is currently using your webcam", NULL);
            } else {
                message = g_strdup ("A process is currently using your webcam");
            }
            // Notify only if not explicitly allowed
            if (!allowed) {
                if (nss != INIT_ERROR) {
                    sg_send_notification ("YOU ARE BEING SNOOPED", message, 5000);
                } else {
                    g_print ("YOU ARE BEING SNOOPED: %s\n", message);
                }
            }
            (void)denied; // reserved for future policy handling
            g_free (proc_name);
            g_free (message);
        } else {
            sg_state_set_webcam(FALSE, NULL);
        }
        close (fd);
    }
}


gint
open_device (const gchar *dev_name)
{
    int fd = open (dev_name, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        g_printerr ("Cannot open '%s': %d, %s\n", dev_name, errno, g_strerror (errno));
        return GENERIC_ERROR;
    }

    struct stat st;
    if (stat (dev_name, &st) == -1) {
        g_printerr ("Cannot identify '%s': %d, %s\n", dev_name, errno, g_strerror (errno));
        close (fd);
        return GENERIC_ERROR;
    }
    if (!S_ISCHR (st.st_mode)) {
        g_printerr ("%s is no device\n", dev_name);
        close (fd);
        return GENERIC_ERROR;
    }

    return fd;
}


gint
init_device (gint fd, const gchar *dev_name)
{
    struct v4l2_capability cap;

    if (xioctl (fd, VIDIOC_QUERYCAP, &cap) == -1) {
        if (errno == EINVAL) {
            g_printerr ("%s is no V4L2 device\n", dev_name);
            return GENERIC_ERROR;
        } else {
            g_printerr ("VIDIOC_QUERYCAP: %s\n", g_strerror (errno));
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        g_printerr ("%s is no video capture device\n", dev_name);
        return GENERIC_ERROR;
    }

    return get_webcam_status (fd, dev_name);
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
            g_printerr ("%s does not support memory mapping\n", dev_name);
            return GENERIC_ERROR;
        } else {
            g_print ("Webcam IS being used\n");
            return WEBCAM_ALREADY_IN_USE;
        }
    } else {
        g_print ("Webcam is NOT being used\n");
        return WEBCAM_NOT_IN_USE;
    }
}


gboolean
ignored_app_using_webcam (const gchar *dev_name, gchar **ignored_apps)
{
    if (ignored_apps == NULL) {
        return FALSE;
    }

    for (guint i = 0, n = (guint)g_strv_length(ignored_apps); i < n; i++) {
        guint pid = get_ppid_from_pname (ignored_apps[i]);
        if (pid != 0) {
            if (get_webcam_from_open_fd (dev_name, pid, FALSE) == WEBCAM_FOUND) {
                return TRUE;
            }
        }
    }

    return FALSE;
}


gint
get_webcam_from_open_fd (const gchar *dev_name, guint pid, gboolean called_from_get_proc)
{
    gchar *pid_str = g_strdup_printf ("%u", pid);
    gchar *path = g_strconcat ("/proc/", pid_str, "/fd", NULL);

    GError *err = NULL;
    GDir *dir = g_dir_open (path, 0, &err);
    if (err != NULL) {
        if (!called_from_get_proc) {
            g_printerr ("%s\n", err->message);
        }
        return GENERIC_ERROR;
    }

    const gchar *subdir = g_dir_read_name (dir);
    while (subdir != NULL) {
        gchar *complete_path = g_strconcat (path, "/", subdir, NULL);
        GFile *file = g_file_new_for_path (complete_path);
        GFileInfo *file_info = g_file_query_info (file, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, &err);
        if (err != NULL) {
            g_printerr("%s\n", err->message);
            g_clear_error(&err);
        } else {
            const gchar *target = g_file_info_get_symlink_target (file_info);
            if (g_strcmp0 (dev_name, target) == 0) {
                g_free (complete_path);
                g_dir_close (dir);
                g_free (pid_str);
                g_free (path);
                g_object_unref (file);
                return WEBCAM_FOUND;
            }
        }
        g_free (complete_path);
        g_object_unref (file);
        subdir = g_dir_read_name (dir);
    }

    g_dir_close (dir);
    g_free (pid_str);
    g_free (path);

    return WEBCAM_NOT_FOUND;
}


gchar *
get_proc_using_webcam (const gchar *webcam_dev)
{
    gchar *file, *contents, **stat_tokens;
    gsize length;
    GError *err = NULL;
    gchar *proc = NULL;

    for (guint i = 1000; i < 40000; i++) {
        if (get_webcam_from_open_fd (webcam_dev, i, TRUE) == WEBCAM_FOUND) {
            gchar *i_str = g_strdup_printf ("%d", i);
            file = g_strconcat ("/proc/", i_str, "/stat", NULL);
            g_file_get_contents (file, &contents, &length, &err);
            stat_tokens = g_strsplit (contents, " ", 3);
            proc = g_malloc0 (strlen(stat_tokens[1]) - 1);  //we remove ( and ) but we add \0
            memcpy (proc, stat_tokens[1] + 1, strlen (stat_tokens[1]) - 2);
            proc[strlen(stat_tokens[1]) -1] = '\0';
            g_strfreev (stat_tokens);
            g_free (i_str);
            g_free (contents);
            g_free (file);
            break;
        }
    }
    return proc;
}
