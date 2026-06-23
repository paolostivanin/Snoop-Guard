#include "sg-policy.h"

static gboolean
strv_contains (gchar **list, const gchar *name)
{
    if (!list || !name) return FALSE;
    for (gchar **entry = list; *entry; entry++) {
        if (g_strcmp0 (*entry, name) == 0) return TRUE;
    }
    return FALSE;
}

gboolean
sg_policy_should_notify (const gchar *process,
                         gchar **allow_list,
                         gchar **deny_list)
{
    if (!process || !*process) return TRUE;
    if (strv_contains (deny_list, process)) return TRUE;
    if (strv_contains (allow_list, process)) return FALSE;
    return TRUE;
}
