#include "core/audio.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ttr {

static const AudioServer k_probe_order[] = {
    AUDIO_PULSE, AUDIO_PIPEWIRE, AUDIO_ALSA
};

AudioServer audio_probe_order(int i)
{
    if (i < 0 || i >= (int)(sizeof(k_probe_order) / sizeof(k_probe_order[0])))
        return AUDIO_NONE;
    return k_probe_order[i];
}

const char* audio_probe_command(AudioServer s)
{
    switch (s) {
    case AUDIO_PULSE: return "pactl info";
    case AUDIO_PIPEWIRE: return "wpctl get-volume @DEFAULT_AUDIO_SINK@";
    case AUDIO_ALSA: return "amixer get Master";
    case AUDIO_NONE: break;
    }
    return "";
}

bool audio_probe_ok(AudioServer s, const char* output)
{
    switch (s) {
    case AUDIO_PULSE: return strstr(output, "Server Name:") != 0;
    case AUDIO_PIPEWIRE: return strncmp(output, "Volume:", 7) == 0;
    case AUDIO_ALSA: return strstr(output, "%]") != 0;
    case AUDIO_NONE: break;
    }
    return false;
}

Str audio_server_name(AudioServer s, const char* probe_output)
{
    switch (s) {
    case AUDIO_PULSE:
        /* "Server Name: PulseAudio (on PipeWire 1.0.5)" */
        if (probe_output && strstr(probe_output, "PipeWire"))
            return Str("PipeWire");
        return Str("PulseAudio");
    case AUDIO_PIPEWIRE: return Str("PipeWire");
    case AUDIO_ALSA: return Str("ALSA");
    case AUDIO_NONE: break;
    }
    return Str("none");
}

const char* audio_get_command(AudioServer s)
{
    switch (s) {
    case AUDIO_PULSE:
        return "pactl get-sink-volume @DEFAULT_SINK@; "
               "pactl get-sink-mute @DEFAULT_SINK@";
    case AUDIO_PIPEWIRE: return "wpctl get-volume @DEFAULT_AUDIO_SINK@";
    case AUDIO_ALSA: return "amixer get Master";
    case AUDIO_NONE: break;
    }
    return "";
}

/* The number before the first '%' in s, or -1. */
static int first_percent(const char* s)
{
    const char* p = strchr(s, '%');
    while (p) {
        const char* q = p;
        while (q > s && isdigit((unsigned char)q[-1]))
            q--;
        if (q < p)
            return atoi(q);
        p = strchr(p + 1, '%');
    }
    return -1;
}

bool audio_parse(AudioServer s, const char* output, VolumeState* v)
{
    v->valid = false;
    switch (s) {
    case AUDIO_PULSE: {
        /* "Volume: front-left: 26214 /  40% / -23.88 dB, ..." "Mute: no" */
        int pct = first_percent(output);
        if (pct < 0)
            return false;
        v->percent = pct;
        v->muted = strstr(output, "Mute: yes") != 0;
        break;
    }
    case AUDIO_PIPEWIRE: {
        /* "Volume: 0.40" or "Volume: 0.40 [MUTED]" */
        const char* p = strstr(output, "Volume:");
        if (!p)
            return false;
        double vol = atof(p + 7);
        v->percent = (int)(vol * 100.0 + 0.5);
        v->muted = strstr(output, "[MUTED]") != 0;
        break;
    }
    case AUDIO_ALSA: {
        /* "Front Left: Playback 65536 [100%] [on]" */
        const char* p = strstr(output, "%]");
        if (!p)
            return false;
        int pct = first_percent(output);
        if (pct < 0)
            return false;
        v->percent = pct;
        const char* sw = strstr(p, "[o");
        v->muted = sw && strncmp(sw, "[off]", 5) == 0;
        break;
    }
    case AUDIO_NONE:
        return false;
    }
    v->valid = true;
    return true;
}

Str audio_set_volume_command(AudioServer s, int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 150)
        percent = 150;
    char buf[128];
    switch (s) {
    case AUDIO_PULSE:
        sprintf(buf, "pactl set-sink-volume @DEFAULT_SINK@ %d%%", percent);
        break;
    case AUDIO_PIPEWIRE:
        sprintf(buf, "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%", percent);
        break;
    case AUDIO_ALSA:
        sprintf(buf, "amixer -q set Master %d%%", percent);
        break;
    case AUDIO_NONE:
        buf[0] = 0;
        break;
    }
    return Str(buf);
}

Str audio_set_mute_command(AudioServer s, int mute)
{
    const char* pa = mute < 0 ? "toggle" : mute ? "1" : "0";
    const char* al = mute < 0 ? "toggle" : mute ? "mute" : "unmute";
    char buf[128];
    switch (s) {
    case AUDIO_PULSE:
        sprintf(buf, "pactl set-sink-mute @DEFAULT_SINK@ %s", pa);
        break;
    case AUDIO_PIPEWIRE:
        sprintf(buf, "wpctl set-mute @DEFAULT_AUDIO_SINK@ %s", pa);
        break;
    case AUDIO_ALSA:
        sprintf(buf, "amixer -q set Master %s", al);
        break;
    case AUDIO_NONE:
        buf[0] = 0;
        break;
    }
    return Str(buf);
}

bool in_path(const char* program)
{
    const char* path = getenv("PATH");
    if (!path)
        path = "/usr/bin:/bin";
    char buf[1024];
    const char* p = path;
    while (*p) {
        const char* e = strchr(p, ':');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && n + strlen(program) + 2 < sizeof(buf)) {
            memcpy(buf, p, n);
            buf[n] = '/';
            strcpy(buf + n + 1, program);
            struct stat st;
            if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) &&
                access(buf, X_OK) == 0)
                return true;
        }
        if (!e)
            break;
        p = e + 1;
    }
    return false;
}

struct KnownPanel {
    const char* program;    /* looked up on $PATH */
    const char* label;
    const char* command;    /* empty: the program itself */
    bool terminal;
};

/* Graphical mixers first, then patchbays, desktop settings, terminal
 * mixers. apanel is the IRIX audio panel. */
static const KnownPanel k_panels[] = {
    { "pavucontrol", "PulseAudio Volume Control (pavucontrol)", "", false },
    { "pwvucontrol", "PipeWire Volume Control (pwvucontrol)", "", false },
    { "pavucontrol-qt", "PulseAudio Volume Control (Qt)", "", false },
    { "gnome-control-center", "GNOME Sound Settings",
      "gnome-control-center sound", false },
    { "systemsettings", "KDE Audio Settings", "systemsettings kcm_pulseaudio",
      false },
    { "mate-volume-control", "MATE Sound Preferences", "", false },
    { "helvum", "Helvum (PipeWire patchbay)", "", false },
    { "qpwgraph", "qpwgraph (PipeWire graph)", "", false },
    { "alsamixer", "ALSA Mixer (alsamixer)", "", true },
    { "pulsemixer", "pulsemixer", "", true },
    { "apanel", "Audio Panel (apanel)", "", false }
};

void find_sound_panels(const char* terminal, Vector<SoundPanel*>* out)
{
    int i;
    for (i = 0; i < (int)(sizeof(k_panels) / sizeof(k_panels[0])); i++) {
        const KnownPanel& k = k_panels[i];
        if (!in_path(k.program))
            continue;
        SoundPanel* p = new SoundPanel;
        p->label = k.label;
        const char* cmd = k.command[0] ? k.command : k.program;
        if (k.terminal && terminal && *terminal) {
            char buf[512];
            sprintf(buf, "%.300s %.200s", terminal, cmd);
            p->command = buf;
        } else {
            p->command = cmd;
        }
        out->push_back(p);
    }
}

}
