#pragma once


void sg_log_init (const gchar *file_path);

gchar *get_log_level_string (GLogLevelFlags log_levels);

void sg_log_handler (const gchar *log_domain, GLogLevelFlags log_levels, const gchar *message, gpointer user_data);

void sg_log_event (const gchar *event);
