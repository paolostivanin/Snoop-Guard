#pragma once
#include <glib.h>

#define SG_LOG_DEFAULT_MAX_BYTES ((gsize) 256 * 1024)

gboolean sg_log_init (const gchar *file_path, gsize max_bytes, GError **error);
void sg_log_set_max_bytes (gsize max_bytes);

void sg_log_uninit (void);

const gchar *sg_log_get_path (void);

void sg_log_event (const gchar *event);

/* For internal/testing use; registered as a glib log handler by sg_log_init. */
void sg_log_handler (const gchar *log_domain,
                     GLogLevelFlags log_levels,
                     const gchar *message,
                     gpointer user_data);
