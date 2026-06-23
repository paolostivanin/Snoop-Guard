#include <glib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "main.h"

typedef enum {
    PROBE_NOT_CAPTURE,
    PROBE_INACTIVE,
    PROBE_ACTIVE,
    PROBE_DEGRADED,
} ProbeResult;

static gint
xioctl (gint fd, gulong request, void *arg)
{
    gint result;
    do {
        result = ioctl (fd, request, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

static ProbeResult
probe_device (const gchar *path)
{
    gint fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return errno == EBUSY ? PROBE_ACTIVE : PROBE_DEGRADED;
    }

    struct stat st;
    if (fstat (fd, &st) != 0 || !S_ISCHR (st.st_mode)) {
        close (fd);
        return PROBE_DEGRADED;
    }

    struct v4l2_capability cap = { 0 };
    if (xioctl (fd, VIDIOC_QUERYCAP, &cap) != 0) {
        close (fd);
        return PROBE_DEGRADED;
    }
    guint32 caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
                 ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) &&
        !(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        close (fd);
        return PROBE_NOT_CAPTURE;
    }

    const enum v4l2_buf_type types[] = {
        V4L2_BUF_TYPE_VIDEO_CAPTURE,
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    };
    ProbeResult result = PROBE_DEGRADED;
    for (guint i = 0; i < G_N_ELEMENTS (types); i++) {
        struct v4l2_requestbuffers req = {
            .count = 0,
            .type = types[i],
            .memory = V4L2_MEMORY_MMAP,
        };
        if (xioctl (fd, VIDIOC_REQBUFS, &req) == 0) {
            result = PROBE_INACTIVE;
            break;
        }
        if (errno == EBUSY) {
            result = PROBE_ACTIVE;
            break;
        }
        if (errno != EINVAL) break;
    }
    close (fd);
    return result;
}

static gboolean
numeric_name (const gchar *name)
{
    if (!name || !*name) return FALSE;
    for (const gchar *p = name; *p; p++) {
        if (!g_ascii_isdigit (*p)) return FALSE;
    }
    return TRUE;
}

static gchar *
read_proc_comm (const gchar *pid)
{
    gchar *path = g_build_filename ("/proc", pid, "comm", NULL);
    gchar *contents = NULL;
    if (g_file_get_contents (path, &contents, NULL, NULL)) {
        g_strchomp (contents);
        if (!g_utf8_validate (contents, -1, NULL)) {
            gchar *valid = g_utf8_make_valid (contents, -1);
            g_free (contents);
            contents = valid;
        }
        for (gchar *p = contents; *p; p++) {
            if ((guchar) *p < 0x20 || (guchar) *p == 0x7f) *p = ' ';
        }
        g_strstrip (contents);
    }
    g_free (path);
    if (!contents || !*contents) {
        g_free (contents);
        return NULL;
    }
    return contents;
}

static void
add_unique (GHashTable *set, const gchar *value)
{
    if (value && *value) g_hash_table_add (set, g_strdup (value));
}

static gint
compare_strings (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (*(gchar * const *) a, *(gchar * const *) b);
}

static gchar **
set_to_sorted_strv (GHashTable *set)
{
    GPtrArray *array = g_ptr_array_new_with_free_func (g_free);
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init (&iter, set);
    while (g_hash_table_iter_next (&iter, &key, NULL)) {
        g_ptr_array_add (array, g_strdup (key));
    }
    g_ptr_array_sort (array, compare_strings);
    g_ptr_array_add (array, NULL);
    return (gchar **) g_ptr_array_free (array, FALSE);
}

static void
scan_proc_holders (GHashTable *devices,
                   GHashTable *active_devices,
                   GHashTable *processes)
{
    GDir *proc = g_dir_open ("/proc", 0, NULL);
    if (!proc) return;

    const gchar *pid;
    while ((pid = g_dir_read_name (proc)) != NULL) {
        if (!numeric_name (pid)) continue;
        gchar *fd_dir = g_build_filename ("/proc", pid, "fd", NULL);
        GDir *fds = g_dir_open (fd_dir, 0, NULL);
        if (!fds) {
            g_free (fd_dir);
            continue;
        }

        gboolean process_matched = FALSE;
        const gchar *fd_name;
        while ((fd_name = g_dir_read_name (fds)) != NULL) {
            gchar *fd_path = g_build_filename (fd_dir, fd_name, NULL);
            gchar target[PATH_MAX];
            ssize_t n = readlink (fd_path, target, sizeof (target) - 1);
            g_free (fd_path);
            if (n < 0 || (gsize) n >= sizeof (target)) continue;
            target[n] = '\0';
            if (g_hash_table_contains (devices, target)) {
                add_unique (active_devices, target);
                process_matched = TRUE;
            }
        }
        g_dir_close (fds);
        g_free (fd_dir);

        if (process_matched) {
            gchar *comm = read_proc_comm (pid);
            add_unique (processes, comm);
            g_free (comm);
        }
    }
    g_dir_close (proc);
}

const gchar *
sg_monitor_health_to_string (SGMonitorHealth health)
{
    switch (health) {
    case SG_MONITOR_OK: return "ok";
    case SG_MONITOR_DEGRADED: return "degraded";
    case SG_MONITOR_UNAVAILABLE: return "unavailable";
    }
    return "unavailable";
}

void
sg_monitor_snapshot_clear (SGMonitorSnapshot *snapshot)
{
    if (!snapshot) return;
    g_clear_pointer (&snapshot->processes, g_strfreev);
    g_clear_pointer (&snapshot->unknown_devices, g_strfreev);
    g_clear_pointer (&snapshot->diagnostic, g_free);
    memset (snapshot, 0, sizeof (*snapshot));
}

gboolean
check_webcams (SGMonitorSnapshot *snapshot)
{
    g_return_val_if_fail (snapshot != NULL, FALSE);
    sg_monitor_snapshot_clear (snapshot);

    struct SGDevice *list = list_webcam ();
    GHashTable *devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *active = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *holders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *processes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *unknown = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    for (struct SGDevice *node = list; node; node = node->next) {
        ProbeResult result = probe_device (node->dev_name);
        if (result == PROBE_NOT_CAPTURE) continue;
        add_unique (devices, node->dev_name);
        if (result == PROBE_ACTIVE) {
            add_unique (active, node->dev_name);
            add_unique (unknown, node->dev_name);
        } else if (result == PROBE_DEGRADED) {
            add_unique (unknown, node->dev_name);
        }
    }
    scan_proc_holders (devices, holders, processes);
    GHashTableIter holder_iter;
    gpointer holder;
    g_hash_table_iter_init (&holder_iter, holders);
    while (g_hash_table_iter_next (&holder_iter, &holder, NULL)) {
        add_unique (active, holder);
        g_hash_table_remove (unknown, holder);
    }

    snapshot->active = g_hash_table_size (active) > 0;
    snapshot->processes = set_to_sorted_strv (processes);
    snapshot->unknown_devices = set_to_sorted_strv (unknown);
    snapshot->health = g_hash_table_size (unknown) > 0
                     ? SG_MONITOR_DEGRADED : SG_MONITOR_OK;
    if (snapshot->health != SG_MONITOR_OK) {
        snapshot->diagnostic = g_strdup (
            "One or more webcam devices could not be attributed or probed reliably");
    }

    g_hash_table_destroy (devices);
    g_hash_table_destroy (active);
    g_hash_table_destroy (holders);
    g_hash_table_destroy (processes);
    g_hash_table_destroy (unknown);
    free_webcam_list (list);
    return TRUE;
}
