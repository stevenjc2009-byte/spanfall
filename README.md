# Spanfall

A falling-sand block puzzler for the PlayStation Vita. Blocks land and crumble into sand.
Join one colour of sand from the left wall to the right wall and that whole region clears.

**Version 1.0.0.** Title ID `SPNF00001`.

Inspired by [Sandtrix](https://sandtrix.net/) by msky-dev. Spanfall is an independent,
fan-made homebrew game with its own code and art, and is not affiliated with Sandtrix.

## How to play

- Tetromino blocks fall into a well, 10 blocks wide and 20 tall. When one lands it turns into
  sand: every block is 8 x 8 grains, and the grains slide and pile up.
- Each piece is one of four colours: red, blue, green or yellow. The next piece's shape and
  colour are shown on the right.
- When one connected region of a single colour touches **both** walls, it flashes and clears.
  Grains that touch at a corner count as connected.
- You score **1 point per grain cleared**, times the combo.
- Clear again within 5 seconds to raise the combo, up to x10. The meter under the combo
  shows the time left.
- The fall speed rises every 40 seconds, up to speed 15.
- The game ends when the sand reaches the top of the well.

Your best score is saved to `ux0:data/Spanfall/best.dat`. Quitting a game from the pause
menu still records its score as a best.

## Controls

| Action | Control |
| --- | --- |
| Move left / right | D-pad left / right (tap for one block, hold to slide) |
| Rotate clockwise | D-pad up or X |
| Drop faster | D-pad down (hold) |
| Pause | START (Resume / Quit to title) |
| Menus | D-pad up / down, X to choose, O to go back (swapped if your Vita uses O to confirm) |

## Install

1. Copy `spanfall.vpk` to your Vita (for example with VitaShell over USB or FTP).
2. In VitaShell, select the VPK and install it.
3. Start **Spanfall** from the LiveArea.

> **Enable unsafe homebrew first.** Spanfall ships as an *unsafe* SELF because its in-game
> updater writes into its own `ux0:app/SPNF00001` folder and restarts itself, and neither is
> allowed to a safe homebrew. Turn it on under
> *Settings > HENkaku Settings > Enable Unsafe Homebrew*. Without it the game will not start.

### Install by QR

Scan to download the newest VPK:

![Install QR for spanfall.vpk](docs/install-qr.png)

`https://github.com/stevenjc2009-byte/spanfall/releases/latest/download/spanfall.vpk`

## Updating from inside the game

Pick **Check for updates** on the title menu. It needs Wi-Fi.

1. Spanfall asks `github.com/stevenjc2009-byte/spanfall/releases/latest` over HTTPS for the
   newest release tag and compares it with its own version.
2. If there is nothing newer it says so. If there is, it shows the version; X starts the
   download, O goes back.
3. The new `spanfall.vpk` is downloaded to `ux0:data/Spanfall/update`, checked, and rejected
   if it is for a different title.
4. Each file is written into `ux0:app/SPNF00001` beside the old one, then renamed into place.
   If the console crashes or loses power mid-install, the next boot finishes the job (or puts
   the old files back) before anything is drawn.
5. On success it offers to restart; X relaunches straight into the new version.

O cancels at any point and leaves the installed game untouched. Your save is never touched
by an update.

The updater is copied unchanged from [Foldwind](https://github.com/stevenjc2009-byte/foldwind)
(only its per-game settings differ). TLS certificates are always verified against the 8 root
certificates shipped in the VPK at `assets/cacert.pem`.

## Building from source

Requirements: [VitaSDK](https://vitasdk.org/) with a libcurl and OpenSSL build, CMake 3.16 or
newer, Python 3 with Pillow (only to regenerate the LiveArea art).

```sh
./build.sh           # release build  -> build/spanfall.vpk
./build.sh pretend   # updater test   -> build-pretend/spanfall.vpk  (never release this one)
bash tests/run.sh    # host unit tests (gcc): sand, pieces, game rules, save file
python3 tools/make_sce_sys.py   # regenerate sce_sys PNGs (8-bit palettized)
```

`build.sh` expects VitaSDK at `~/vitasdk` and CMake at `~/tools/cmake-3.30.5-linux-x86_64/bin`.
Edit the two `export` lines at the top of the script if yours are somewhere else.

`./build.sh pretend` makes the **updater only** believe the installed version is 0.0.0, so a
check finds the published release and installs it for real. The game still shows its real
version.

`src/version.h` holds `SF_VERSION` ("MAJOR.MINOR.PATCH"). CMake turns it into the param.sfo
APP_VER `MM.PP` (1.0.0 becomes 01.00, 1.2.3 becomes 01.23), so MINOR and PATCH must stay 0-9.

## Project layout

```
src/main.c        entry point, frame loop, updater boot sweep and shutdown
src/app.c         screens (title, play, pause, game over, updates) and their flow
src/game.c        game rules: pieces, gravity, clears, combo, speed, game over (pure C)
src/sand.c        the sand grid: falling grains and wall-to-wall region search (pure C)
src/piece.c       tetromino shapes, rotation, collision, turning a piece into sand (pure C)
src/render.c      vita2d drawing: the well texture, HUD, panels, text
src/input.c       pad reading and the enter-button setting
src/save.c        best-score save file with .tmp/.bak fallbacks (pure C + stdio)
src/update_screen.c  updater state -> screen text and button actions (from Foldwind)
src/updater/      in-game updater core (from Foldwind)
tests/            host tests + run.sh
tools/            LiveArea art generator
sce_sys/          LiveArea assets
assets/cacert.pem CA roots shipped in the VPK, the updater's only trust source
```

## Known limitations

- **Not yet tested on real hardware.** It has been played in the Vita3K emulator only.
- **The in-game updater has never run on a real Vita.** It has worked end to end in Vita3K,
  but only hardware can prove the Wi-Fi/TLS path, writing into its own `ux0:app` folder and
  the restart.
- Endless mode only. No sound or music.

## License

GPL-3.0. See [LICENSE](LICENSE).
