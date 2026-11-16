#include "puzzle_cache.h"

#include <stdlib.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "generator.h"
#include "levels.h"
#include "puzzle_bank.h"

static const char *TAG = "puzzles";

#define MAX_LEVELS 32

static struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    int current;  // level on screen
    bool ready[MAX_LEVELS];
    puzzle_t puzzles[MAX_LEVELS];
    int last_banked[MAX_LEVELS];  // bank puzzle shown last, so it is not repeated straight away
} s;

static void generate(int level, puzzle_t *out)
{
    uint32_t seed = esp_random();
    puzzle_gen_stats_t stats;
    const int64_t begin = esp_timer_get_time();
    puzzle_generate(out, &levels[level], &seed, LEVEL_GENERATION_STEPS, &stats);
    ESP_LOGI(TAG, "Puzzle %d ready in %lld ms: %d clues, %s, %d attempts, %ld solver steps", level + 1,
             (esp_timer_get_time() - begin) / 1000, stats.clues, stats.unique ? "one solution" : "several solutions",
             stats.attempts, stats.solver_steps);
}

// A random puzzle from the pre-computed bank, never the one shown last time.
// Returns false if the bank has none for this level.
static bool from_bank(int level, puzzle_t *out)
{
    if (level >= puzzle_bank_levels || puzzle_bank[level].count == 0) {
        return false;
    }
    const int count = puzzle_bank[level].count;
    int pick = esp_random() % count;
    if (count > 1 && pick == s.last_banked[level]) {
        pick = (pick + 1 + esp_random() % (count - 1)) % count;
    }
    s.last_banked[level] = pick;
    ESP_LOGI(TAG, "Puzzle %d taken from the bank (%d of %d)", level + 1, pick + 1, count);
    return puzzle_parse(out, puzzle_bank[level].maps[pick]);
}

// The level to prepare next, or -1 if every level has a puzzle waiting: the
// next level first, then this one and the previous, then outwards.
static int pick_level(void)
{
    const int current = s.current;
    const int order[3] = {current + 1, current, current - 1};
    for (int i = 0; i < 3; i++) {
        if (order[i] >= 0 && order[i] < level_count && !s.ready[order[i]]) {
            return order[i];
        }
    }
    for (int distance = 2; distance < level_count; distance++) {
        for (int sign = 1; sign >= -1; sign -= 2) {
            const int level = current + sign * distance;
            if (level >= 0 && level < level_count && !s.ready[level]) {
                return level;
            }
        }
    }
    return -1;
}

static void cache_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        const int level = pick_level();
        xSemaphoreGive(s.lock);
        if (level < 0) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        puzzle_t puzzle;
        generate(level, &puzzle);

        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.puzzles[level] = puzzle;
        s.ready[level] = true;
        xSemaphoreGive(s.lock);
    }
}

void puzzle_cache_start(void)
{
    if (level_count > MAX_LEVELS) {
        abort();
    }
    for (int i = 0; i < MAX_LEVELS; i++) {
        s.last_banked[i] = -1;
    }
    s.lock = xSemaphoreCreateMutex();
    // Lowest priority, on the core the game loop does not use.
    xTaskCreatePinnedToCore(cache_task, "puzzles", 12288, NULL, 1, &s.task, 1);
}

void puzzle_cache_take(int level, puzzle_t *out)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.current = level;
    const bool ready = s.ready[level];
    if (ready) {
        *out = s.puzzles[level];
        s.ready[level] = false;
    }
    xSemaphoreGive(s.lock);

    if (!ready && !from_bank(level, out)) {
        uint32_t seed = esp_random();
        puzzle_generate(out, &levels[level], &seed, 4000, NULL);  // no bank: a quick puzzle on the spot
    }
    if (s.task) {
        xTaskNotifyGive(s.task);  // prepare the next puzzle for this level and its neighbours
    }
}
