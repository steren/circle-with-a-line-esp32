# Circle with a Line

A line-drawing puzzle game for the **Waveshare ESP32-S3-Touch-AMOLED-1.8** (1.8" 368×448
AMOLED touch board, no battery), written in plain C on ESP-IDF.

> **Board revision:** the unit on this desk is the **V2** hardware (CO5300 display driver,
> CST820 touch), even though it was sold as SH8601 + FT3168. Waveshare switched shipments
> to V2 in 2026. The game detects the revision at boot by probing the touch
> controller's I2C address (`0x15` = V2, `0x38` = original) and logs it over serial.

## Layout

| Folder | What it is |
| --- | --- |
| [`main/`](main/) | The game |
| [`tools/`](tools/) | PC tool: tests, puzzle generator and bank, screenshots, sound render |
| [`components/amoled-board/`](components/amoled-board/) | Board support: display, touch, buttons, audio, drawing |

The repository root is the ESP-IDF project (`CMakeLists.txt`, `sdkconfig.defaults`). Build
output goes to `build/` (firmware) and `build-host/` (PC tool, screenshots, sounds).

## The game

An original puzzle game for the touch screen. Each puzzle glows in one flat, saturated
colour, like a backlit acrylic or electroluminescent panel, softly darkening towards the
edges of the screen. The board is a grid of tiles separated by deep channels of the same
hue. Drag a line along the channels from the start (a large solid circle)
to the exit (a short rounded cap sticking out past the border) without crossing your own line. The tip of the line slides with
your finger, rounds corners you cut, and backs up when you retrace it. Lifting your finger at the exit
submits the answer (the line runs into the cap); lifting it anywhere else leaves the line in place so you can pick it
up again from its head. Touching the start circle starts over.

The line has to satisfy every clue on the board:

| Clue | Looks like | Rule |
| --- | --- | --- |
| Beacon | hexagon in a channel or at a crossing | the line must pass through it |
| Gap | a break in the channel | the line cannot go that way |
| Count | 1–3 orange triangles on a tile | that many of the tile's four sides are part of the line |
| Square | rounded square on a tile | the line splits the board into areas; no area may hold squares of two colours |
| Star | 8-pointed star on a tile | its area must hold exactly two symbols (stars or squares) of its colour |
| Polyomino | gold Tetris shape on a tile | its area must be tiled by the shapes inside it, without rotation |
| Negator | white 3-pronged Y on a tile | cancels one broken symbol in its area (fails if nothing in its area is broken) |

There are 50 levels, from 2×2 to 5×5 (including 2×3, 3×4, and 4×5 vertical rectangular grids), each new rule introduced gently on a small board before combining with earlier rules. Every
puzzle is generated afresh each time its level is opened, so it is never the same twice
(see below). The screen shows only the board. A correct answer turns the line green and moves on to the next
puzzle; a wrong one flashes red and marks the clues it broke.

| Button | Action |
| --- | --- |
| BOOT, short press | next puzzle |
| PWR, short press | previous puzzle |
| either, held for 3 s | power off |
| either, held for 1 s (while off) | power on |

Sound comes from the board's speaker. While your finger is drawing, a steady hum plays,
and it fades out when you lift your finger. Solving, a wrong answer and the buttons each have a short
phrase. Everything is synthesized in `main/synth.c`; change `HUM_AMPLITUDE` and
`NOTE_AMPLITUDE` there to make it louder or quieter.

`make game-test` also renders these sounds to `build-host/sounds.wav`,
so you can hear them on the PC.

```sh
make game-test       # on this computer: rules self-test, generator check, simulated play
make game-build      # compile the firmware
make game-flash      # compile + upload to /dev/ttyACM0
make game-monitor    # serial log (Ctrl+] to quit)
```

| File | What it does |
| --- | --- |
| `main/puzzle.c`, `puzzle.h` | The rules: parsing puzzle maps, moving along the grid, checking an answer |
| `main/game.c`, `game.h` | The game: layout, colours, drawing, touch handling, puzzle progression |
| `main/levels.c` | The levels: size and clue types of each |
| `main/generator.c`, `generator.h` | Puzzle generator and solver |
| `main/puzzle_cache.c`, `puzzle_cache.h` | Board only: generates puzzles in the background, falls back on the bank |
| `main/puzzle_bank.c` | 2,000 pre-computed puzzles, written by `make game-bank` |
| `main/synth.c`, `synth.h` | Sound generator: the drawing hum and the short phrases |
| `main/sound.c`, `sound.h` | Runs the generator in its own task and feeds the speaker |
| `main/main.c` | Connects the game to the board's touch, display, buttons and speaker |
| `tools/puzzle_tool.c` | PC-side tool: tests, puzzle generator, screenshots |

`puzzle.c` and `game.c` have no hardware dependencies, so `make game-test` compiles
them with `cc` and exercises them on the PC: it checks the rules, confirms every puzzle is
solvable, plays each one through with simulated touches, and saves screenshots to
`build-host/shots/` (PPM files).

### How puzzles are made

`main/levels.c` lists what each level is (its size and which clues it uses), not the
puzzles themselves. `main/generator.c` builds a puzzle to order: it draws a random line,
derives clues from it, then removes clues one at a time for as long as a solver can prove
the puzzle still has exactly one solution.

On the board this runs in the background on the second CPU core, keeping a fresh puzzle
ready for every level, starting with the next one. Most levels take well under a second;
the hardest 5×5 ones can take up to about 20 s. If you open a level before its fresh puzzle
is ready, you get a random one from a bank of 40 per level pre-computed on the PC
(`main/puzzle_bank.c`), never the same one twice in a row. The serial log shows which.

```sh
make game-bank                                              # regenerate the bank (after changing levels.c)
build-host/puzzle_tool gen 4 4 ngb 7     # print one puzzle: columns, rows, rules, seed
```

The rules argument is any mix of `n` (triangles), `g` (squares), `s` (stars), `p` (polyominoes), `y` (negators), `b` (hexagons) and `x` (gaps).
`make game-test` generates 100 puzzles for every level and reports how often they have a
single solution and how long they take.

## Setup

One-time setup (Arch/Omarchy):

```sh
sudo pacman -S --needed cmake ninja ccache dfu-util
git clone -b v6.0.2 --recursive --depth 1 --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32s3
```

The `game-*` make targets load ESP-IDF themselves. To use `idf.py` directly instead, run
`. ~/esp/esp-idf/export.sh` first, then `idf.py build flash monitor` in
the project root. Set `IDF_PATH` if the SDK lives somewhere other than `~/esp/esp-idf`.

### Board support component

| File | What it does |
| --- | --- |
| `components/amoled-board/board.c` | Pin map, I2C, reset via the IO expander, revision detection, touch, buttons |
| `components/amoled-board/audio.c` | ES8311 codec and I2S set-up, speaker output |
| `components/amoled-board/display.c` | QSPI panel driver; sends the framebuffer (held in PSRAM) to the panel |
| `components/amoled-board/gfx.c` | Drawing into the framebuffer: rectangles, circles, text, lighting |
| `components/amoled-board/font5x7.h` | 5x7 bitmap font (from Adafruit GFX, BSD licence) |

### Serial port and flashing

Serial port access on Arch/Omarchy (one time, no re-login needed):

```sh
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-espressif.rules
sudo udevadm control --reload && sudo udevadm trigger
```

If an upload ever fails to connect: hold **BOOT**, power-cycle the board (hold **PWR** ≥ 6 s
to turn off, press again to turn on), release BOOT, then flash.

## Hardware

| Part | Chip | Bus | Notes |
| --- | --- | --- | --- |
| MCU | ESP32-S3R8 | — | 16 MB flash, 8 MB octal PSRAM, native USB (serial/JTAG) |
| Display | CO5300 (V2) / SH8601 (original) | QSPI | 1.8" AMOLED, 368×448, RGB565; CO5300 needs a 16-column offset |
| Touch | CST820 (V2) / FT3168 (original) | I2C `0x15` / `0x38` | INT on GPIO21; same register layout on both |
| IO expander | TCA9554 | I2C `0x20` | drives display/touch reset and power |
| PMU | AXP2101 | I2C `0x34` | |
| RTC | PCF85063A | I2C `0x51` | |
| IMU | QMI8658 | I2C `0x6B` | 6-axis |
| Audio | ES8311 | I2S + I2C `0x18` | mic + speaker amp |
| Storage | microSD | SDMMC | |

### Pin map

| Function | GPIO |
| --- | --- |
| LCD CS / SCLK | 12 / 11 |
| LCD SDIO0–3 | 4, 5, 6, 7 |
| I2C SDA / SCL (shared by all I2C chips) | 15 / 14 |
| Touch INT | 21 |
| I2S MCLK / BCLK / WS | 16 / 9 / 45 |
| I2S data to codec / from codec | 8 / 10 |
| Speaker amp enable | 46 |
| SD CLK / CMD / DATA | 2 / 1 / 3 |
| BOOT button | 0 |

| Expander pin | Net |
| --- | --- |
| EXIO0 | LCD reset |
| EXIO1 | Display power enable (`DSI_PWR_EN`) |
| EXIO2 | Touch reset |
| EXIO4 | PWR button (high when pressed) |

Sources: Waveshare's `pin_config.h` and the schematic. (arduino-esp32's own board variant
for this board has `SDA`/`SCL` swapped relative to these, as 14/15.)

## Documentation

**Board**
- [Product documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8) · [Arduino guide](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8/Arduino) · [Resources](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8/Resources-And-Documents)
- [Legacy wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8) · [Product page](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)
- [Schematic (PDF)](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8.pdf) · [3D model](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8-3D.zip)
- [Official examples repo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8) — for this V2 board use `examples/arduino-v2/` (CO5300/CST820), not `examples/arduino/` (original: SH8601/FT3168); the ESP-IDF examples in `examples/esp-idf/` handle both
- [ESP-IDF board support component](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8)

**Datasheets**
- [CO5300 display driver (V2)](https://files.waveshare.com/wiki/common/CO5300_Datasheet_V0.00.pdf) · [SH8601 display driver (original)](https://files.waveshare.com/wiki/common/SH8601A0_DataSheet_Preliminary_V0.0_UCS_191107_1.pdf)
- [FT3168 touch controller (original)](https://files.waveshare.com/wiki/common/FT3168.pdf) — Waveshare publishes no CST820 datasheet; their Arduino code drives it as a CST816
- [AXP2101 PMU](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
- [QMI8658 IMU](https://files.waveshare.com/wiki/common/QMI8658C.pdf)
- [PCF85063A RTC](https://files.waveshare.com/wiki/common/PCF85063A.pdf)
- [ES8311 codec](https://files.waveshare.com/wiki/common/ES8311.DS.pdf) · [ES8311 user guide](https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf)
- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) · [Technical reference manual](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf)

**Software**
- [ESP-IDF programming guide (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/index.html) · [ESP-IDF source](https://github.com/espressif/esp-idf)

## License

[Apache License 2.0](LICENSE). `components/amoled-board/font5x7.h` comes from the Adafruit GFX
library and keeps its own BSD licence, reproduced at the top of that file.
