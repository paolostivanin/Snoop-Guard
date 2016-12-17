#define _GNU_SOURCE
#include <glib.h>
#include <gio/gio.h>
#include <alsa/asoundlib.h>
#include "main.h"


gint check_sysdefault_dev ();


gint
get_mic_status (const gchar *mic)
{
    if (g_strcmp0 (mic, "sysdefault") == 0) {
        gint status = check_sysdefault_dev ();
        if (status != SYSDEFAULT_FOUND) {
            return status;
        }
    }

    snd_pcm_t *capture_handle = NULL;

    if (snd_pcm_open (&capture_handle, mic, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        return MIC_ALREADY_IN_USE;
    }
    else {
        snd_pcm_close (capture_handle);
        return MIC_NOT_IN_USE;
    }
}


gint
check_sysdefault_dev ()
{
    GError *err = NULL;
    GSubprocess *sp = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE, &err, "/usr/bin/arecord", "-L", NULL);
    if (sp == NULL) {
        g_print ("%s\n", err->message);
        g_clear_error (&err);
        return GENERIC_ERROR;
    }

    gchar *output = NULL;
    if (!g_subprocess_communicate_utf8 (sp, NULL, NULL, &output, NULL, &err)) {
        g_print ("%s\n", err->message);
        g_clear_error (&err);
        return GENERIC_ERROR;
    }
    if (g_strstr_len (output, -1, "sysdefault:CARD") == NULL) {
        g_free(output);
        return SYSDEFAULT_FOUND;
    } else {
        g_free(output);
        return SYSDEFAULT_DEV_NOT_FOUND;
    }
}