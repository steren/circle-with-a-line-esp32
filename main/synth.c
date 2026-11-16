#include "synth.h"

#include <math.h>

#define TWO_PI (2.0f * (float)M_PI)

// The hum: one steady note. The board's speaker is very quiet below about
// 500 Hz (measured with its own microphone), so the hum is built mostly from
// harmonics: the ear still hears the low note, but the sound that carries is
// an octave or two above it.
#define HUM_AMPLITUDE 4800.0f  // of 32767
#define HUM_HZ 440.0f
#define HUM_DETUNE 1.006f      // a second, slightly sharp voice makes it beat gently
#define HUM_FADE 0.0015f       // per sample: fades in and out over about 40 ms
#define HUM_SILENT 0.001f      // gain below which the hum counts as off

// The phrases: plucked notes played one after another.
#define NOTE_AMPLITUDE 6300.0f
#define NOTE_ATTACK_MS 4       // fade-in that keeps a note from starting with a click
#define CUT_SAMPLES 64         // fade-out when a new phrase cuts off the old one
#define MAX_NOTES 4

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
} note_t;

static struct {
    bool hum_on;
    float hum_gain, hum_phase, hum_phase2;

    note_t notes[MAX_NOTES];
    int note_count, note_index, note_position;
    float note_phase, note_envelope;

    bool next_waiting;  // a phrase is waiting for the current one to fade out
    game_sound_t next;
    int cut_left;
} s;

static int notes_for(game_sound_t sound, note_t notes[MAX_NOTES])
{
    switch (sound) {
    case GAME_SOUND_SOLVED:
        notes[0] = (note_t){523, 90};
        notes[1] = (note_t){659, 90};
        notes[2] = (note_t){784, 90};
        notes[3] = (note_t){1047, 350};
        return 4;
    case GAME_SOUND_FAILED:
        notes[0] = (note_t){233, 140};
        notes[1] = (note_t){175, 300};
        return 2;
    case GAME_SOUND_RESET:
        notes[0] = (note_t){392, 80};
        return 1;
    case GAME_SOUND_LEVEL:
        notes[0] = (note_t){784, 50};
        notes[1] = (note_t){1047, 80};
        return 2;
    default:
        return 0;
    }
}

static float wrap(float phase)
{
    return phase >= TWO_PI ? phase - TWO_PI : phase;
}

void synth_hum_on(void)
{
    s.hum_on = true;
}

void synth_hum_off(void)
{
    s.hum_on = false;
}

static void start_phrase(game_sound_t sound)
{
    s.note_count = notes_for(sound, s.notes);
    s.note_index = 0;
    s.note_position = 0;
    s.next_waiting = false;
}

void synth_phrase(game_sound_t sound)
{
    if (s.note_index < s.note_count) {
        s.next = sound;
        if (!s.next_waiting) {
            s.next_waiting = true;
            s.cut_left = CUT_SAMPLES;
        }
    } else {
        start_phrase(sound);
    }
}

bool synth_is_silent(void)
{
    return !s.hum_on && s.hum_gain < HUM_SILENT && s.note_index >= s.note_count && !s.next_waiting;
}

void synth_render(int16_t *samples, int count)
{
    for (int i = 0; i < count; i++) {
        float sample = 0;

        // Hum: a voice rich in harmonics, and a second one a hair sharp of it.
        s.hum_gain += ((s.hum_on ? 1.0f : 0.0f) - s.hum_gain) * HUM_FADE;
        if (s.hum_on || s.hum_gain >= HUM_SILENT) {
            sample += HUM_AMPLITUDE * s.hum_gain * 0.3f *
                      (sinf(s.hum_phase) + 0.8f * sinf(2 * s.hum_phase) + 0.5f * sinf(3 * s.hum_phase) +
                       0.25f * sinf(4 * s.hum_phase) + 0.6f * sinf(s.hum_phase2) + 0.5f * sinf(2 * s.hum_phase2));
            const float step = TWO_PI * HUM_HZ / SYNTH_SAMPLE_RATE;
            s.hum_phase = wrap(s.hum_phase + step);
            s.hum_phase2 = wrap(s.hum_phase2 + step * HUM_DETUNE);
        } else {
            s.hum_gain = 0;
        }

        // Phrase: a sine with a little second harmonic and a decaying volume.
        if (s.note_index < s.note_count) {
            const note_t *note = &s.notes[s.note_index];
            const int total = SYNTH_SAMPLE_RATE * note->duration_ms / 1000;
            const int attack = SYNTH_SAMPLE_RATE * NOTE_ATTACK_MS / 1000;
            if (s.note_position == 0) {
                s.note_phase = 0;
                s.note_envelope = 1.0f;
            }
            float gain = s.note_envelope;
            if (s.note_position < attack) {
                gain *= (float)s.note_position / attack;
            }
            if (s.next_waiting) {
                gain *= (float)s.cut_left / CUT_SAMPLES;
            }
            sample += NOTE_AMPLITUDE * gain * (sinf(s.note_phase) + 0.25f * sinf(2 * s.note_phase));
            s.note_phase = wrap(s.note_phase + TWO_PI * note->frequency_hz / SYNTH_SAMPLE_RATE);
            s.note_envelope *= expf(-3.0f / total);  // falls to 5% over the note
            if (++s.note_position >= total) {
                s.note_position = 0;
                s.note_index++;
            }
        }
        if (s.next_waiting && (--s.cut_left <= 0 || s.note_index >= s.note_count)) {
            start_phrase(s.next);
        }

        samples[i] = (int16_t)sample;
    }
}
