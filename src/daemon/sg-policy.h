#pragma once

#include <glib.h>

gboolean sg_policy_should_notify (const gchar *process,
                                  gchar **allow_list,
                                  gchar **deny_list);
