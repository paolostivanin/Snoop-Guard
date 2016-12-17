#define _GNU_SOURCE
#include <glib.h>
#include <alsa/asoundlib.h>
#include "main.h"


gint
get_mic_status (const gchar *mic)
{
    // TODO: grep capture /proc/asound/devices. If 1 then sysdefault? And if 2 what? DEAL WITH MULTIPLE MIC
    /* TODO arecord -L | grep -w sysdefault:CARD with system() could be a solution. Check the fork() thing to add more security
     * (https://www.securecoding.cert.org/confluence/pages/viewpage.action?pageId=2130132)
     */
    snd_pcm_t *capture_handle = NULL;

    if (snd_pcm_open (&capture_handle, mic, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        // TODO This fails also if the device name is not correct. Device presence should be checked
        return MIC_ALREADY_IN_USE;
    }
    else {
        snd_pcm_close (capture_handle);
        return MIC_NOT_IN_USE;
    }
}