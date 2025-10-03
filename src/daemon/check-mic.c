#define _GNU_SOURCE
#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "main.h"
#include "sg-state.h"

static gboolean path_is_capture_pcm(const gchar *path)
{
    // Simple heuristic: /dev/snd/pcmC*D*c (ending with 'c' means capture device)
    if (!path) return FALSE;
    if (g_str_has_prefix(path, "/dev/snd/pcm") && g_str_has_suffix(path, "c")) return TRUE;
    return FALSE;
}

static gchar* read_comm(guint pid)
{
    gchar *comm_path = g_strdup_printf("/proc/%u/comm", pid);
    gchar *content = NULL; gsize len = 0;
    if (g_file_get_contents(comm_path, &content, &len, NULL)) {
        g_strchomp(content);
        g_free(comm_path);
        return content;
    }
    g_free(comm_path);
    return NULL;
}

static gboolean process_uses_capture(guint pid, gchar **proc_name_out)
{
    gchar *fd_dir = g_strdup_printf("/proc/%u/fd", pid);
    DIR *dir = opendir(fd_dir);
    if (!dir) { g_free(fd_dir); return FALSE; }
    gboolean found = FALSE;
    struct dirent *de;
    gchar link_path[PATH_MAX];
    gchar target[PATH_MAX];
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        g_snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir, de->d_name);
        ssize_t r = readlink(link_path, target, sizeof(target)-1);
        if (r > 0) {
            target[r] = '\0';
            if (path_is_capture_pcm(target)) {
                found = TRUE;
                break;
            }
        }
    }
    closedir(dir);
    if (found && proc_name_out) {
        *proc_name_out = read_comm(pid);
    }
    return found;
}

static gboolean any_process_uses_mic(gchar **proc_name_out)
{
    DIR *proc = opendir("/proc");
    if (!proc) return FALSE;
    struct dirent *de; gboolean active = FALSE; gchar *name = NULL;
    while ((de = readdir(proc)) != NULL) {
        if (de->d_type != DT_DIR) continue;
        guint pid = (guint)g_ascii_strtoull(de->d_name, NULL, 10);
        if (pid == 0) continue;
        if (process_uses_capture(pid, &name)) { active = TRUE; break; }
    }
    closedir(proc);
    if (active && proc_name_out) *proc_name_out = name; else if (name) g_free(name);
    return active;
}

int get_mic_status (const gchar *mic __attribute__((unused)))
{
    gchar *proc = NULL;
    gboolean active = any_process_uses_mic(&proc);
    sg_state_set_mic(active, proc);
    if (proc) g_free(proc);
    if (active) {
        g_print("Mic IS being used\n");
        return MIC_ALREADY_IN_USE;
    } else {
        g_print("Mic is NOT being used\n");
        return MIC_NOT_IN_USE;
    }
}

int check_sysdefault_dev ()
{
    // We no longer depend on ALSA to validate sysdefault; assume present on modern systems.
    return SYSDEFAULT_FOUND;
}