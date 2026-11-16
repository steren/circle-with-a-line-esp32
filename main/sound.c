#include "sound.h"

#include "audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "synth.h"

static const char *TAG = "sound";

#define CHUNK_SAMPLES 64  // 4 ms of sound is produced at a time

_Static_assert(SYNTH_SAMPLE_RATE == AUDIO_SAMPLE_RATE, "the synthesizer must run at the codec's sample rate");

static TaskHandle_t s_task;
static QueueHandle_t s_requests;

// Runs the synthesizer: applies requests from the game, and feeds the speaker
// for as long as there is something to hear.
static void sound_task(void *arg)
{
    (void)arg;
    for (;;) {
        game_sound_t sound;
        while (xQueueReceive(s_requests, &sound, synth_is_silent() ? portMAX_DELAY : 0) == pdTRUE) {
            if (sound == GAME_SOUND_TRACE) {
                synth_hum_on();
            } else {
                synth_hum_off();
                if (sound != GAME_SOUND_LIFT) {
                    synth_phrase(sound);
                }
            }
        }
        int16_t samples[CHUNK_SAMPLES];
        synth_render(samples, CHUNK_SAMPLES);
        audio_write(samples, CHUNK_SAMPLES);
    }
}

void sound_init(void)
{
    const esp_err_t err = audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Audio did not start (%s); running silent", esp_err_to_name(err));
        return;
    }
    s_requests = xQueueCreate(8, sizeof(game_sound_t));
    xTaskCreate(sound_task, "sound", 4096, NULL, 5, &s_task);
}

void sound_play(game_sound_t sound)
{
    if (s_task) {
        xQueueSend(s_requests, &sound, 0);  // if the queue is ever full, drop the request
    }
}
