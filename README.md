# 🦎 Axolotl Adventure

**by AliceFriend**

A cheerful underwater side-scroller for kids, made for the ESP32 **"Cheap Yellow Display"** (CYD). Guide your axolotl through the water, dodge rocks, seaweed and fish, and collect bubbles and hearts along the way.

## How to play

| Touch | What happens |
|---|---|
| **Top half** of the screen | Swim up ⬆️ |
| **Bottom half** of the screen | Swim down ⬇️ |
| **Pause button** (top-right) | Pause the game |

A quick tap gives a burst of speed, and holding keeps swimming. When you let go, the axolotl gently glides to a stop.

- 🪨 Rocks on the sea floor and hanging from above
- 🌿 Swaying seaweed
- 🐟 Fish that swim toward you
- 🫧 Bubbles give you a point
- ❤️ Hearts give you back a life

You also get a point for every obstacle you pass. Every 10 points you'll get a little cheer!

## Features

- **Seven axolotl skins.** Unlock new ones by beating your best score.
- **Three levels:** Easy (5 hearts), Normal and Zoom!
- **Kid-friendly design:** hearts instead of instant game over, forgiving hitboxes, a short safe time after each bump, and friendly messages
- **Sound effects** with Off / Quiet / Loud settings
- **The RGB LED** on the back flashes on bubbles, bumps and milestones
- **Saves automatically:** best score, chosen skin, level and volume are remembered after power-off
- Flicker-free graphics at around 25 frames per second

### Skins

| Skin | How to unlock |
|---|---|
| Pinky | Available from the start |
| Goldie | Available from the start |
| Wild | Available from the start |
| Sky | Best score of 15 |
| Minty | Best score of 25 |
| Midnight | Best score of 40 |
| Rainbow | Best score of 60 |

## Hardware

- **ESP32-2432S028R**, the original CYD with a 2.8" ILI9341 screen, resistive touch and micro-USB
- A small **speaker** (optional) that plugs into the 2-pin `SPEAK` connector

> The newer "CYD2USB" boards with two USB ports use a different display chip. They may need `ST7789_DRIVER` and colour inversion in `User_Setup.h`.

## Installation

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add the **esp32** board package by Espressif in the Boards Manager.
3. Install these libraries in the Library Manager:
   - **TFT_eSPI** by Bodmer
   - **XPT2046_Touchscreen** by Paul Stoffregen
4. Copy `TFT_eSPI_Setup/User_Setup.h` over the file with the same name in your Arduino libraries folder, in `libraries/TFT_eSPI/`.
   > ⚠️ The screen won't work without this step. If you update TFT_eSPI later, you'll need to copy the file again.
5. Open `AxolotlAdventure/AxolotlAdventure.ino` in the Arduino IDE.
6. Choose **Tools → Board → ESP32 Dev Module** and your CYD's port.
7. Click **Upload**.

## Troubleshooting

| Problem | Fix |
|---|---|
| Touches land in the wrong place | Set `TOUCH_DEBUG` to `1` at the top of the sketch. It shows where the board thinks you touched and the raw numbers, so you can adjust `TOUCH_X_MIN/MAX` and `TOUCH_Y_MIN/MAX` to match. |
| Up and down are swapped | Set `TOUCH_FLIP_Y` to `1` |
| The picture is upside down | Change `tft.setRotation(1)` to `tft.setRotation(3)` in `setup()` |
| White screen or wrong colours | Try `ILI9341_DRIVER` instead of `ILI9341_2_DRIVER` in `User_Setup.h` |
| Glitchy or garbled picture | Lower `SPI_FREQUENCY` to `40000000` in `User_Setup.h` |
| Crashes or boot loops | Try version **2.0.17** of the esp32 board package |
| No sound | Check a speaker is plugged into `SPEAK` and the sound setting isn't **Off** |

## Customising

Most settings are near the top of `AxolotlAdventure.ino`:

- `IMPULSE`, `SWIM_ACC` and `MAX_VY` change how the axolotl swims
- `LEVELS[]` sets the speed, obstacle spacing and number of hearts for each level
- `SKINS[]` sets skin colours and unlock scores. Adding a new line creates a new skin.
- The `SND_...` arrays hold the sound effects as lists of `{frequency in Hz, length in ms}`

## Project layout

```
axolotl-adventure/
├── AxolotlAdventure/
│   ├── AxolotlAdventure.ino   Main game
│   └── GameTypes.h            Shared game data types
├── TFT_eSPI_Setup/
│   └── User_Setup.h           Display setup for the CYD
├── LICENSE
└── README.md
```

## License

Released under the [MIT License](LICENSE).
