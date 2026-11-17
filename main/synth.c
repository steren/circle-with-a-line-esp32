#include "synth.h"

#include <math.h>
#include <string.h>

#define TWO_PI (2.0f * (float)M_PI)

// The board's speaker rolls off steeply below ~500 Hz, so low tones are built
// with strong 2nd-5th harmonics: the ear still perceives the warm low pitch,
// while the upper harmonics carry clearly through the small speaker.
#define HUM_AMPLITUDE 6600.0f  // of 32767
#define HUM_HZ 220.0f          // warm A3 fundamental (heard via 440/660/880 Hz harmonics)
#define HUM_DETUNE 1.005f      // slightly sharp chorus voice for gentle beating
#define HUM_LFO_HZ 2.6f        // slow electrical pulse while tracing
#define HUM_FADE_IN 0.0025f    // ~25 ms smooth swell when touching the start node
#define HUM_FADE_OUT 0.0022f   // fades to < HUM_SILENT within ~200 ms on lift
#define HUM_SILENT 0.001f

#define NOTE_AMPLITUDE 8200.0f
#define CUT_SAMPLES 64         // smooth fade-out when a new phrase interrupts an active one

static struct {
    bool hum_on;
    float hum_gain, hum_bloom, hum_pitch, hum_target_pitch;
    float step_pulse;
    bool step_back;
    float hum_phase, hum_phase2, hum_lfo_phase;

    game_sound_t phrase;
    int phrase_pos, phrase_len;
    float p1, p2, p3, p4, p5;  // oscillator phases for polyphonic/swept phrases

    bool next_waiting;
    game_sound_t next;
    int cut_left;
} s;

static float wrap(float phase)
{
    while (phase >= TWO_PI) {
        phase -= TWO_PI;
    }
    return phase;
}

static int phrase_duration_samples(game_sound_t sound)
{
    switch (sound) {
    case GAME_SOUND_SOLVED:
        return SYNTH_SAMPLE_RATE * 800 / 1000;  // latch click + two-step chime + cable power surge
    case GAME_SOUND_FAILED:
        return SYNTH_SAMPLE_RATE * 460 / 1000;  // low dissonant electrical rejection buzz
    case GAME_SOUND_LEVEL:
        return SYNTH_SAMPLE_RATE * 130 / 1000;  // subtle panel wake-up fifth
    default:
        return 0;
    }
}

void synth_hum_on(void)
{
    if (!s.hum_on) {
        s.hum_bloom = 1.0f;  // subtle initial activation pulse when grabbing the start circle
        s.hum_pitch = 1.0f;
        s.hum_target_pitch = 1.0f;
    }
    s.hum_on = true;
}

void synth_hum_off(void)
{
    s.hum_on = false;
}

void synth_trace_step(int path_length, bool stepped_back)
{
    const int steps = path_length > 1 ? path_length - 1 : 0;
    float target = 1.0f + 0.014f * (float)steps;
    if (target > 1.35f) target = 1.35f;
    s.hum_target_pitch = target;
    s.step_pulse = 1.0f;
    s.step_back = stepped_back;
}

static void start_phrase(game_sound_t sound)
{
    s.phrase = sound;
    s.phrase_pos = 0;
    s.phrase_len = phrase_duration_samples(sound);
    s.p1 = s.p2 = s.p3 = s.p4 = s.p5 = 0.0f;
    s.next_waiting = false;
}

void synth_phrase(game_sound_t sound)
{
    if (s.phrase_pos < s.phrase_len) {
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
    return !s.hum_on && s.hum_gain < HUM_SILENT && s.phrase_pos >= s.phrase_len && !s.next_waiting;
}

static float render_phrase_sample(void)
{
    const float t = (float)s.phrase_pos / SYNTH_SAMPLE_RATE;
    const int attack = SYNTH_SAMPLE_RATE * 5 / 1000;
    float env = s.phrase_pos < attack ? (float)s.phrase_pos / attack : 1.0f;
    float out = 0.0f;

    switch (s.phrase) {
    case GAME_SOUND_SOLVED: {
        // 1. Mechanical/electrical relay latch as the line locks into the exit cap.
        const float latch_freq = 196.0f + 340.0f * expf(-t * 55.0f);
        s.p1 = wrap(s.p1 + TWO_PI * latch_freq / SYNTH_SAMPLE_RATE);
        const float latch = expf(-t * 38.0f) * (0.6f * sinf(s.p1) + 0.4f * sinf(2.0f * s.p1));

        // 2. First resonant tone (G4 = 392 Hz + upper harmonics), swelling in at t = 0.
        s.p2 = wrap(s.p2 + TWO_PI * 392.00f / SYNTH_SAMPLE_RATE);
        const float env1 = (1.0f - expf(-t * 80.0f)) * expf(-t * 5.2f);
        const float tone1 = env1 * (0.35f * sinf(s.p2) + 0.45f * sinf(2.0f * s.p2) + 0.20f * sinf(3.0f * s.p2));

        // 3. Second ascending chime (D5 = 587.33 Hz + G5 = 783.99 Hz + B5 = 987.77 Hz
        // with a slightly detuned chorus voice) blooming in at t = 110 ms.
        float tone2 = 0.0f;
        if (t >= 0.11f) {
            const float dt = t - 0.11f;
            s.p3 = wrap(s.p3 + TWO_PI * 587.33f / SYNTH_SAMPLE_RATE);  // D5
            s.p4 = wrap(s.p4 + TWO_PI * 783.99f / SYNTH_SAMPLE_RATE);  // G5
            s.p5 = wrap(s.p5 + TWO_PI * 787.20f / SYNTH_SAMPLE_RATE);  // G5 chorus shimmer
            const float env2 = (1.0f - expf(-dt * 45.0f)) * expf(-dt * 3.6f);
            tone2 = env2 * (0.38f * sinf(s.p3) + 0.34f * sinf(s.p4) + 0.22f * sinf(s.p5) +
                            0.16f * sinf(2.0f * s.p3));
        }

        // 4. Rising electrical cable power-surge hum beneath the chime.
        const float surge_env = (1.0f - expf(-t * 14.0f)) * expf(-t * 4.5f);
        const float flutter = 0.85f + 0.15f * sinf(TWO_PI * 20.0f * t);
        const float surge = surge_env * flutter * (0.4f * sinf(2.0f * s.p1) + 0.3f * sinf(3.0f * s.p1));

        out = 0.45f * latch + 0.75f * tone1 + 0.95f * tone2 + 0.35f * surge;
        break;
    }
    case GAME_SOUND_FAILED: {
        // Low, gritty dissonant electrical buzz / rejection thud: two closely-spaced
        // low fundamentals (minor second + tritone) rich in 3rd-6th harmonics, sagging
        // in pitch with a double-thud envelope.
        const float droop = 1.0f - 0.18f * (t / 0.46f);
        const float f1 = 146.8f * droop;  // D3
        const float f2 = 155.6f * droop;  // Eb3 (minor second beating against D3)
        const float f3 = 207.7f * droop;  // G#3 (tritone)

        s.p1 = wrap(s.p1 + TWO_PI * f1 / SYNTH_SAMPLE_RATE);
        s.p2 = wrap(s.p2 + TWO_PI * f2 / SYNTH_SAMPLE_RATE);
        s.p3 = wrap(s.p3 + TWO_PI * f3 / SYNTH_SAMPLE_RATE);

        const float pulse1 = expf(-t * 14.0f);
        const float dt = t - 0.11f;
        const float pulse2 = t >= 0.11f ? (1.0f - expf(-dt * 60.0f)) * expf(-dt * 8.5f) : 0.0f;
        const float buzz_env = 0.70f * pulse1 + 0.90f * pulse2;

        const float v1 = 0.25f * sinf(s.p1) + 0.45f * sinf(3.0f * s.p1) + 0.30f * sinf(5.0f * s.p1);
        const float v2 = 0.25f * sinf(s.p2) + 0.45f * sinf(3.0f * s.p2) + 0.25f * sinf(4.0f * s.p2);
        const float v3 = 0.30f * sinf(2.0f * s.p3) + 0.20f * sinf(3.0f * s.p3);
        out = 1.65f * buzz_env * (0.45f * v1 + 0.40f * v2 + 0.25f * v3);
        break;
    }
    case GAME_SOUND_LEVEL: {
        // Soft resonant two-step wake pulse when switching panels.
        s.p1 = wrap(s.p1 + TWO_PI * 440.0f / SYNTH_SAMPLE_RATE);
        s.p2 = wrap(s.p2 + TWO_PI * 659.25f / SYNTH_SAMPLE_RATE);
        const float env1 = expf(-t * 24.0f);
        const float dt = t - 0.04f;
        const float env2 = dt > 0.0f ? (1.0f - expf(-dt * 250.0f)) * expf(-dt * 22.0f) : 0.0f;
        out = 0.65f * env1 * (0.7f * sinf(s.p1) + 0.3f * sinf(2.0f * s.p1)) +
              0.85f * env2 * (0.7f * sinf(s.p2) + 0.3f * sinf(2.0f * s.p2));
        break;
    }
    default:
        break;
    }

    // Fade the last 4 ms smoothly to zero so a phrase never ends with a step.
    const int tail = SYNTH_SAMPLE_RATE * 4 / 1000;
    const int left = s.phrase_len - s.phrase_pos;
    if (left < tail) {
        env *= (float)left / tail;
    }
    if (s.next_waiting) {
        env *= (float)s.cut_left / CUT_SAMPLES;
    }
    return NOTE_AMPLITUDE * env * out;
}

void synth_render(int16_t *samples, int count)
{
    for (int i = 0; i < count; i++) {
        float sample = 0.0f;

        // Tracing hum: warm resonant electrical drone with slow LFO shimmer,
        // initial activation bloom on touch, and slight downward pitch release on lift.
        const float rate = s.hum_on ? HUM_FADE_IN : HUM_FADE_OUT;
        s.hum_gain += ((s.hum_on ? 1.0f : 0.0f) - s.hum_gain) * rate;
        if (s.hum_on || s.hum_gain >= HUM_SILENT) {
            if (s.hum_on) {
                if (s.hum_target_pitch < 0.9f) s.hum_target_pitch = 1.0f;
                s.hum_pitch += (s.hum_target_pitch - s.hum_pitch) * 0.002f;
            } else if (s.hum_pitch > 0.84f) {
                s.hum_pitch -= 0.00006f;  // gentle power-down pitch sag when lifting finger
            }
            s.hum_bloom *= 0.9992f;       // ~120 ms resonant bloom on touch-begin
            s.step_pulse *= 0.9975f;      // ~40 ms subtle resonant pulse on each grid node
            const float lfo = 0.88f + 0.12f * sinf(s.hum_lfo_phase);
            const float p = s.hum_phase, p2 = s.hum_phase2;
            const float node_shimmer = s.step_pulse * (s.step_back ? 0.30f * sinf(2.0f * p) : 0.32f * sinf(4.0f * p));
            const float voice = 0.20f * sinf(p) + 0.55f * sinf(2.0f * p) + 0.38f * sinf(3.0f * p) +
                                0.20f * sinf(4.0f * p) + 0.35f * sinf(2.0f * p2) + 0.22f * sinf(3.0f * p2) +
                                0.35f * s.hum_bloom * sinf(3.0f * p) + node_shimmer;
            sample += HUM_AMPLITUDE * s.hum_gain * lfo * 0.42f * voice;

            const float step = TWO_PI * (HUM_HZ * s.hum_pitch) / SYNTH_SAMPLE_RATE;
            s.hum_phase = wrap(s.hum_phase + step);
            s.hum_phase2 = wrap(s.hum_phase2 + step * HUM_DETUNE);
            s.hum_lfo_phase = wrap(s.hum_lfo_phase + TWO_PI * HUM_LFO_HZ / SYNTH_SAMPLE_RATE);
        } else {
            s.hum_gain = 0.0f;
        }

        if (s.phrase_pos < s.phrase_len) {
            sample += render_phrase_sample();
            s.phrase_pos++;
        }
        if (s.next_waiting && (--s.cut_left <= 0 || s.phrase_pos >= s.phrase_len)) {
            start_phrase(s.next);
        }

        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        samples[i] = (int16_t)sample;
    }
}
