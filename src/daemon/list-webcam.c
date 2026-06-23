#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include "main.h"

/* Enumerate /dev/video* nodes that exist and are character devices. We do not
 * open them here (open() may fail with EBUSY on busy drivers, which would
 * incorrectly drop the device the user actually wants flagged). */
struct SGDevice *
list_webcam (void)
{
    struct SGDevice *head = NULL;
    GError *err = NULL;
    GDir *dir = g_dir_open ("/dev", 0, &err);
    if (!dir) {
        if (err) g_clear_error (&err);
        return NULL;
    }
    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        if (!g_str_has_prefix (name, "video")) continue;
        /* Only accept "video" followed by digits. */
        const gchar *p = name + 5;
        if (!*p) continue;
        gboolean numeric = TRUE;
        while (*p) {
            if (*p < '0' || *p > '9') { numeric = FALSE; break; }
            p++;
        }
        if (!numeric) continue;

        gchar *path = g_strconcat ("/dev/", name, NULL);
        GStatBuf st;
        if (g_stat (path, &st) != 0 || !S_ISCHR (st.st_mode)) {
            g_free (path);
            continue;
        }
        struct SGDevice *node = g_new0 (struct SGDevice, 1);
        node->dev_name = path; /* takes ownership */
        node->next = head;
        head = node;
    }
    g_dir_close (dir);
    return head;
}

void
free_webcam_list (struct SGDevice *head)
{
    while (head) {
        struct SGDevice *next = head->next;
        g_free (head->dev_name);
        g_free (head);
        head = next;
    }
}
