#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include "sg-logging.h"

static const gchar *log_file_path = NULL;

void
sg_log_init (const gchar *file_path)
{
    log_file_path = file_path;
    g_log_set_handler (NULL,
                       G_LOG_LEVEL_INFO | G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_RECURSION,
                       sg_log_handler,
                       NULL);
}


gchar *
get_log_level_string (GLogLevelFlags log_levels)
{
    if (log_levels & G_LOG_LEVEL_INFO) {
        return g_strdup ("[INFO]    ");
    } else if (log_levels & G_LOG_LEVEL_MESSAGE) {
        return g_strdup ("[MESSAGE] ");
    } else if (log_levels & G_LOG_LEVEL_WARNING) {
        return g_strdup ("[WARNING] ");
    } else {
        return g_strdup ("[CRITICAL]");
    }
}


void
sg_log_handler (const gchar *log_domain __attribute__((unused)),
                GLogLevelFlags log_levels,
                const gchar *message,
                gpointer user_data)
{
    (void)user_data;
    const gchar *log_file = log_file_path;
    GDateTime *dt = g_date_time_new_now_local ();
    gchar *dts = g_date_time_format (dt, "%T %F");

    GFile *lf = g_file_new_for_path (log_file);

    gchar *log_level = get_log_level_string (log_levels);
    gchar *fm = g_strconcat ("[", dts, "]", " - ", log_level, " - ", message, "\n", NULL);

    GError *err = NULL;
    GFileOutputStream *lf_os = g_file_append_to (lf, G_FILE_CREATE_NONE, NULL, &err);
    if (err != NULL) {
        g_printerr ("%s\n", err->message);
        g_clear_error (&err);
    } else {
        g_output_stream_write (G_OUTPUT_STREAM (lf_os), fm, strlen(fm), NULL, &err);
        if  (err != NULL) {
            g_printerr ("%s\n", err->message);
            g_clear_error (&err);
        }
        g_object_unref (lf_os);
    }

    g_date_time_unref (dt);
    g_free (dts);
    g_object_unref (lf);
    g_free (log_level);
    g_free (fm);
}
