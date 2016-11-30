#include <glib.h>
#include <glib/gstdio.h>


guint get_ppid_from_pname (const gchar *pname)
{
    gchar *file, *name, *contents, **stat_tokens;
    gsize length;
    GError *err = NULL;
    guint pid = 0;

    for (gint i = 1000; i < 30000; i++) {
        gchar *i_str = g_strdup_printf ("%d", i);
        file = g_strconcat ("/proc/", i_str, "/stat", NULL);
        g_free (i_str);
        g_file_get_contents (file, &contents, &length, &err);
        if (err != NULL) {
            g_clear_error (&err);
            g_free (file);
        } else {
            stat_tokens = g_strsplit (contents, " ", 5);
            name = g_strconcat ("(", pname, ")", NULL);
            if (g_strcmp0 (name, stat_tokens[1]) == 0) {
                if (g_strcmp0 (stat_tokens[3], "1") == 0) {
                    sscanf (stat_tokens[0], "%u", &pid);
                } else {
                    sscanf (stat_tokens[3], "%u", &pid);
                }
                g_strfreev (stat_tokens);
                g_free (contents);
                g_free (file);
                g_free (name);
                break;
            }
            g_strfreev (stat_tokens);
            g_free (contents);
            g_free (file);
            g_free (name);
        }
    }

    return pid;
}