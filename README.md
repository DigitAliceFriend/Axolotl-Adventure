# 🦎 Axolotl Adventure

**by AliceFriend** · version 1.2 (see [CHANGELOG.md](CHANGELOG.md))

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
| 🪙 Coin | 1 coin to spend in the Shop (no points) |

## Coins and the Shop

Collect coins floating in the water, and earn a coin bonus for every level you finish (10 coins plus 5 for each level number, so level 3 gives 25). Spend them in the **Shop**, which opens from the main menu or from the level-complete screen. The Shop isn't available while paused.

Buying takes two taps, so coins aren't spent by accident.

- **Perfect levels:** finish a level without getting bumped for 10 bonus coins.
- **Revive:** when you run out of hearts, you can pay 25 coins to keep swimming with 2 hearts. You can do this once per level.

## Stickers

Earn stickers by reaching goals. Each one pays coins:

| Sticker | Goal | Coins |
|---|---|---|
| First Swim | Finish level 1 | 5 |
| Worm Muncher | Eat 50 worms | 10 |
| Worm Feast | Eat 500 worms | 30 |
| Bubble Popper | Pop 100 bubbles | 10 |
| Coin Collector | Pick up 250 coins | 20 |
| Trophy Hunter | Find a golden trophy | 20 |
| Flawless | Finish a level with no bumps | 15 |
| Perfect Five | 5 levels with no bumps | 30 |
| Explorer | Finish level 5 | 20 |
| Deep Diver | Finish level 10 | 40 |
| Fashionista | Own 5 hats | 20 |
| Best Friends | Unlock a friend to play as | 20 |
| Super Swimmer | Score 500 in one game | 30 |
| Legend | Finish level 26 | 100 |

Tap a sticker on the Stickers screen to see how close you are.

| Tab | What you can buy |
|---|---|
| **Hats** | 18 hats: beanies in red, orange, yellow, green, blue, purple and pastel, plus a pink bow, ball caps, party hats (plain, sparkly, flashy and rainbow), a top hat, a **Santa hat**, a wizard hat and a gold crown. Tap a hat you own to wear it or take it off. |
| **Skins** | 8 special axolotl skins with effects (Glitter, Frosty, Cotton Candy, Glow, Disco, Lava, Galaxy and Golden), plus the pastel collection and Dusky, which you can buy early instead of waiting for their level |
| **Power** | **Shields** (10 coins). In a game, tap the shield button next to pause to be safe from bumps for 5 seconds. Carry up to 5. |
| **Friends** | Hire a friend as a **helper** for the next level (15 coins). It swims in circles around you and grabs any treats and coins it touches. Or **unlock** a friend for good and play as them! |

### Axolotl friends

| Friend | Unlock price | Bonus when you play as them |
|---|---|---|
| Seahorse | 120 | Pulls nearby treats and coins toward you |
| Shrimp | 100 | Tiny, so it's easier to dodge things |
| Turtle | 150 | Gets 1 extra heart |
| Puffer | 130 | Shields last 8 seconds instead of 5 |

Pick who you play as in **Shop → Friends**. Hats work on every friend too.
A helper stays with you until you finish the level, even if you run out of hearts and try again.

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
- The scenery changes each level: Sunny Lagoon, Coral Reef, Sunset Cove, Kelp Forest and Deep Sea, then back to the start.
- **Sunny Lagoon** and **Coral Reef** are shallow: the water stops partway up and there's open sky above, which you can't swim into. Hanging rocks become floating logs, and everything is scaled to fit, so there's always room to swim.
- **Sunset Cove** has the sun shining down through the water, blurry and wobbling with the waves.
- Hearts refill at the start of each level.
- If you run out of hearts, you try the same level again. You never lose a level you've finished.
- Your score keeps adding up across levels until you run out of hearts or go back to the menu.

## Features

- **Player profiles.** Up to 6 players, each with their own name, level, skins and best score. Everything is saved and survives power-off.
- **Leaderboard.** The top 10 scores, with player names and the level reached.
- **35 axolotl skins**: 27 unlocked one per level finished, including a pastel collection, (with a **NEW!** badge until you've looked at them) and 8 special ones in the Shop
- **Coins, a Shop, hats, shields and helper friends**, plus four friends you can play as
- **Five underwater places** that take turns level by level: Sunny Lagoon, Coral Reef, Sunset Cove, Kelp Forest and Deep Sea
- **Pause menu.** Change your skin or sound volume in the middle of a game, then keep swimming. Quitting needs two taps, so little fingers don't end a game by accident.
- **Three speeds:** Easy (5 hearts), Normal and Zoom!
- **Kid-friendly design:** hearts instead of instant game over, forgiving hitboxes, a short safe time after each bump, and lots of cheering
- **Sound effects** with Off / Quiet / Loud settings, and **background music** that you can turn on or off
- **Settings** for speed, sound, music and screen brightness
- **Stickers** to collect, a **New best!** cheer when you beat your record, perfect-level bonuses and revives
- **The RGB LED** on the back flashes on treats, bumps and milestones
- **Screen dims** after a minute without a touch on the menus, to save power. Any touch wakes it.
- A little bubble trail follows your axolotl as it swims
- **Full colour** (65,000 colours) for smooth pastels, sparkles and glows
- Flicker-free graphics at around 20–25 frames per second

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
| Tangerine (bright orange) | Finish level 17 |
| Slate (dark gray) | Finish level 18 |
| Blossom (pastel pink) | Finish level 19 |
| Peach (pastel peach) | Finish level 20 |
| Butter (pastel yellow) | Finish level 21 |
| Pistachio (pastel green) | Finish level 22 |
| Seafoam (pastel aqua) | Finish level 23 |
| Baby Blue (pastel blue) | Finish level 24 |
| Lilac (pastel purple) | Finish level 25 |
| Dusky (fades between blue, pink and purple) | Finish level 26 |

The pastel collection and Dusky can also be bought early in the Shop:

| Skin | Price |
|---|---|
| Blossom | 75 |
| Peach | 80 |
| Butter | 85 |
| Pistachio | 90 |
| Seafoam | 95 |
| Baby Blue | 100 |
| Lilac | 110 |
| Dusky | 150 |

**Shop skins:** Glitter (sparkly pink), Frosty (sparkly ice blue), Cotton Candy (pastel pink and blue), Glow (glowing green), Disco (flashes pink and blue), Lava (glowing orange and red), Galaxy (twinkling stars) and Golden (sparkly gold). Flashing effects swap colours less than twice a second, so they stay gentle on the eyes.

## Menus

- **Who's playing?** appears at start-up when there's more than one player. Tap your name, tap **+ New** to add a player, or tap **Delete** to remove one.
- **Main menu:** Shop, Play, Scores (leaderboard), Skin, Stickers, Settings and Player (switch player)
- **Settings:** Speed, Sound, Music and Brightness
- **Level complete:** Next, Shop and Menu
- **Pause menu:** Keep Swimming, Skin, Sound, Music and Quit to Menu

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
5. Open `AxolotlAdventure/AxolotlAdventure.ino` in the Arduino IDE. The whole game is in this one file.
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
| Game feels slow or jerky | Set `FULL_COLOUR` to `0` near the top of the sketch. It uses 256 colours and draws a little faster. |
| Crashes or boot loops | Try version **2.0.17** of the esp32 board package |
| No sound | Check a speaker is plugged into `SPEAK` and the sound setting isn't **Off** |
| Want to wipe all saved players and scores | In Arduino IDE, set **Tools → Erase All Flash Before Sketch Upload → Enabled** and upload again |

## Customising

Most settings are near the top of `AxolotlAdventure.ino`:

- `WORM_POINTS`, `BUBBLE_POINTS`, `TROPHY_POINTS` and `PASS_POINTS` set what each thing is worth
- `TROPHY_MIN_WORMS` and `TROPHY_MAX_WORMS` control how often golden trophies appear
- `levelCoinBonus()`, `PERFECT_BONUS`, `REVIVE_PRICE`, `SHIELD_PRICE`, `HELPER_PRICE` and `MAX_SHIELDS` set coin rewards and prices
- `STICKERS[]` lists the stickers, their goals and rewards
- `MUSIC[]` is the background tune, written as `{frequency in Hz, length in ms}` notes
- `HATS[]`, the shop part of `SKINS[]` and `FRIENDS[]` list everything in the Shop, with prices. Add new items to the **end** of each list so saved players keep what they've bought.
- `FULL_COLOUR` switches between 65,000 colours (`1`) and 256 colours (`0`)
- `levelGoal()` sets the points needed for each level
- `IMPULSE`, `SWIM_ACC` and `MAX_VY` change how the axolotl swims
- `MODES[]` sets the speed, obstacle spacing and hearts for Easy, Normal and Zoom!
- `SKINS[]` sets the skin colours. Each level skin's last number is the level that unlocks it. Add new skins to the **end** of the list so saved players keep the skins they've earned.
- `THEMES[]` sets the colours of each underwater place, and whether it's shallow (`TK_SHALLOW`), has the sunset sun (`TK_SUNSET`), kelp (`TK_KELP`) or is plain deep water (`TK_DEEP`)
- `GAME_VERSION` is the version number shown on the title screen
- `DIM_AFTER_MS`, `BRIGHT_FULL` and `BRIGHT_DIM` control the screen dimming
- The `SND_...` arrays hold the sound effects as lists of `{frequency in Hz, length in ms}`

## Project layout

```
axolotl-adventure/
├── AxolotlAdventure/
│   └── AxolotlAdventure.ino   The whole game
├── TFT_eSPI_Setup/
│   └── User_Setup.h           Display setup for the CYD
├── CHANGELOG.md
├── LICENSE
└── README.md
```

## License

Released under the [MIT License](LICENSE).
