# 🦎 Axolotl Adventure

**by AliceFriend**

A cheerful underwater side-scroller for kids, made for the ESP32 **"Cheap Yellow Display"** (CYD). Guide your axolotl through the water, dodge rocks, seaweed and fish, eat worms, pop bubbles, and swim through level after level to unlock new axolotl skins.

## How to play

| Touch | What happens |
|---|---|
| **Top half** of the screen | Swim up ⬆️ |
| **Bottom half** of the screen | Swim down ⬇️ |
| **Pause button** (top-right) | Opens the pause menu |

A quick tap gives a burst of speed, and holding keeps swimming. When you let go, the axolotl gently glides to a stop.

Dodge 🪨 rocks on the sea floor and hanging from above, 🌿 swaying seaweed, and 🐟 fish that swim toward you.

### Points

| What | Points |
|---|---|
| 🐛 Eating a worm | 5 |
| 🫧 Popping a bubble | 2 |
| 🏆 Golden trophy (rare! about once every 50–200 worms) | 20 |
| 🪨 Swimming past an obstacle | 1 |
| ❤️ Heart | Gives back a life |

## Levels

Each level has a points goal. Reach it to finish the level and unlock a new skin.

| Level | Points needed |
|---|---|
| 1 | 50 |
| 2 | 75 |
| 3 | 100 |
| 4 | 150 |
| 5 | 200 |
| 6 and up | 100 more than the level before (300, 400, 500, ...) |

- The speed stays the same on every level. Later levels just take longer.
- The scenery changes each level: Sunny Lagoon, Coral Reef, Sunset Bay, Deep Sea, then back to the start.
- Hearts refill at the start of each level.
- If you run out of hearts, you try the same level again. You never lose a level you've finished.
- Your score keeps adding up across levels until you run out of hearts or go back to the menu.

## Features

- **Player profiles.** Up to 6 players, each with their own name, level, skins and best score. Everything is saved and survives power-off.
- **Leaderboard.** The top 10 scores, with player names and the level reached.
- **17 axolotl skins**, unlocked one per level finished, with a **NEW!** badge until you've looked at them
- **Four underwater places** that take turns level by level: Sunny Lagoon, Coral Reef, Sunset Bay and Deep Sea
- **Pause menu.** Change your skin or sound volume in the middle of a game, then keep swimming. Quitting needs two taps, so little fingers don't end a game by accident.
- **Three speeds:** Easy (5 hearts), Normal and Zoom!
- **Kid-friendly design:** hearts instead of instant game over, forgiving hitboxes, a short safe time after each bump, and lots of cheering
- **Sound effects** with Off / Quiet / Loud settings
- **The RGB LED** on the back flashes on treats, bumps and milestones
- **Screen dims** after a minute without a touch on the menus, to save power. Any touch wakes it.
- A little bubble trail follows your axolotl as it swims
- Flicker-free graphics at around 25 frames per second

### Skins

| Skin | How to unlock |
|---|---|
| Pinky | Available from the start |
| Goldie | Finish level 1 |
| Wild | Finish level 2 |
| Sky | Finish level 3 |
| Minty | Finish level 4 |
| Magenta | Finish level 5 |
| Cyan | Finish level 6 |
| Lavender | Finish level 7 |
| White | Finish level 8 |
| Midnight | Finish level 9 |
| Rainbow | Finish level 10 |
| Ruby (red) | Finish level 11 |
| Lemon (yellow) | Finish level 12 |
| Cloud (light gray) | Finish level 13 |
| Shadow (deep black) | Finish level 14 |
| Forest (dark green) | Finish level 15 |
| Twilight (dark rainbow) | Finish level 16 |

## Menus

- **Who's playing?** appears at start-up when there's more than one player. Tap your name, tap **+ New** to add a player, or tap **Delete** to remove one.
- **Main menu:** Play, Scores (leaderboard), Player (switch player), Skin, Speed and Sound
- **Pause menu:** Keep Swimming, Skin, Sound and Quit to Menu

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
5. Open `AxolotlAdventure/AxolotlAdventure.ino` in the Arduino IDE. `GameTypes.h` must be in the same folder.
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
| Want to wipe all saved players and scores | In Arduino IDE, set **Tools → Erase All Flash Before Sketch Upload → Enabled** and upload again |

## Customising

Most settings are near the top of `AxolotlAdventure.ino`:

- `WORM_POINTS`, `BUBBLE_POINTS`, `TROPHY_POINTS` and `PASS_POINTS` set what each thing is worth
- `TROPHY_MIN_WORMS` and `TROPHY_MAX_WORMS` control how often golden trophies appear
- `levelGoal()` sets the points needed for each level
- `IMPULSE`, `SWIM_ACC` and `MAX_VY` change how the axolotl swims
- `MODES[]` sets the speed, obstacle spacing and hearts for Easy, Normal and Zoom!
- `SKINS[]` sets the skin colours. Their order is the unlock order. Add new skins to the end so saved players keep the skins they've earned.
- `THEMES[]` sets the colours of each underwater place
- `DIM_AFTER_MS`, `BRIGHT_FULL` and `BRIGHT_DIM` control the screen dimming
- The `SND_...` arrays hold the sound effects as lists of `{frequency in Hz, length in ms}`

## Project layout

```
axolotl-adventure/
├── AxolotlAdventure/
│   ├── AxolotlAdventure.ino   Main game
│   └── GameTypes.h            Shared game data types
├── TFT_eSPI_Setup/
│   └── User_Setup.h           Display setup for the CYD
├── CHANGELOG.md
├── LICENSE
└── README.md
```

## License

Released under the [MIT License](LICENSE).
