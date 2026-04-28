#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "sg-logging.h"

static gchar *log_file_path = NULL;
static gsize  log_max_bytes = SG_LOG_DEFAULT_MAX_BYTES;

static const gchar *
log_level_string (GLogLevelFlags log_levels)
{
    if (log_levels & G_LOG_LEVEL_INFO)     return "[INFO]    ";
    if (log_levels & G_LOG_LEVEL_MESSAGE)  return "[MESSAGE] ";
    if (log_levels & G_LOG_LEVEL_WARNING)  return "[WARNING] ";
    if (log_levels & G_LOG_LEVEL_CRITICAL) return "[CRITICAL]";
    if (log_levels & G_LOG_LEVEL_ERROR)    return "[ERROR]   ";
    return "[UNKNOWN] ";
}

static void
maybe_rotate (void)
{
    if (!log_file_path) return;
    GStatBuf st;
    if (g_stat (log_file_path, &st) != 0) return;
    if ((gsize) st.st_size < log_max_bytes) return;

    gchar *backup = g_strconcat (log_file_path, ".1", NULL);
    g_unlink (backup);
    if (g_rename (log_file_path, backup) != 0) {
        /* ignore: continue appending to the existing file */
    }
    g_free (backup);
}

static void
write_line (const gchar *line)
{
    if (!log_file_path || !line) return;
    maybe_rotate ();
    int fd = g_open (log_file_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    /* Best-effort: tighten perms in case the file pre-existed with a wider mask. */
    (void) fchmod (fd, 0600);
    gsize len = strlen (line);
    while (len > 0) {
        ssize_t w = write (fd, line, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        line += w;
        len  -= (gsize) w;
    }
    (void) close (fd);
}

void
sg_log_init (const gchar *file_path, gsize max_bytes)
{
    if (!file_path) return;
    g_free (log_file_path);
    log_file_path = g_strdup (file_path);
    log_max_bytes = max_bytes > 0 ? max_bytes : SG_LOG_DEFAULT_MAX_BYTES;
    gchar *dir = g_path_get_dirname (file_path);
    g_mkdir_with_parents (dir, 0700);
    g_free (dir);
    g_log_set_handler (NULL,
                       G_LOG_LEVEL_INFO | G_LOG_LEVEL_MESSAGE |
                       G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL |
                       G_LOG_FLAG_RECURSION,
                       sg_log_handler, NULL);
}

void
sg_log_uninit (void)
{
    g_clear_pointer (&log_file_path, g_free);
}

const gchar *
sg_log_get_path (void)
{
    return log_file_path;
}

void
sg_log_handler (const gchar *log_domain G_GNUC_UNUSED,
                GLogLevelFlags log_levels,
                const gchar *message,
                gpointer user_data G_GNUC_UNUSED)
{
    if (!log_file_path) return;
    GDateTime *dt = g_date_time_new_now_local ();
    gchar *dts = g_date_time_format (dt, "%F %T");
    gchar *line = g_strconcat ("[", dts, "] - ",
                               log_level_string (log_levels), " - ",
                               message ? message : "",
                               "\n", NULL);
    write_line (line);
    g_free (line);
    g_free (dts);
    g_date_time_unref (dt);
}

void
sg_log_event (const gchar *event)
{
    if (!log_file_path || !event) return;
    GDateTime *dt = g_date_time_new_now_local ();
    gchar *dts = g_date_time_format (dt, "%F %T");
    gchar *line = g_strconcat (dts, " ", event, "\n", NULL);
    write_line (line);
    g_free (line);
    g_free (dts);
    g_date_time_unref (dt);
}
