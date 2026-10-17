/*
 * Master volume of the default output, through the sound server's own
 * command line tools, so ttr links no audio libraries and a hung server
 * cannot hang the window manager (the shell runs these asynchronously).
 *
 * Servers are probed in order: PulseAudio protocol (pactl; also PipeWire
 * through pipewire-pulse), PipeWire native (wpctl), ALSA (amixer). IRIX
 * (libaudio) can be added as another backend behind the same interface.
 */
#ifndef TTR_CORE_AUDIO_H
#define TTR_CORE_AUDIO_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

enum AudioServer {
    AUDIO_NONE,
    AUDIO_PULSE,        /* pactl: PulseAudio, or PipeWire via pipewire-pulse */
    AUDIO_PIPEWIRE,     /* wpctl */
    AUDIO_ALSA          /* amixer */
};

struct VolumeState {
    bool valid;
    int percent;        /* 0 .. 150 or so; servers allow over 100 */
    bool muted;

    VolumeState() : valid(false), percent(0), muted(false) {}
};

/* The probe for each server, in order, and how to read its answer. */
AudioServer audio_probe_order(int i);       /* AUDIO_NONE after the last */
const char* audio_probe_command(AudioServer s);
bool audio_probe_ok(AudioServer s, const char* output);

/* Display name, e.g. "PipeWire" or "PulseAudio"; for pactl the server's
 * own name from the probe output decides. */
Str audio_server_name(AudioServer s, const char* probe_output);

const char* audio_get_command(AudioServer s);
bool audio_parse(AudioServer s, const char* output, VolumeState* v);
Str audio_set_volume_command(AudioServer s, int percent);
/* mute: 1 on, 0 off, -1 toggle. */
Str audio_set_mute_command(AudioServer s, int mute);

/* A sound control program found on this system. */
struct SoundPanel {
    Str label;
    Str command;
};

/* Known sound panels that are installed (on $PATH), best first. Terminal
 * programs are run with 'terminal' (e.g. "xterm -e"). */
void find_sound_panels(const char* terminal, Vector<SoundPanel*>* out);

/* True if 'program' is an executable file on $PATH. */
bool in_path(const char* program);

}

#endif
