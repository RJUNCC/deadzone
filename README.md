# Deadzone

![Deadzone shaping a controller's sticks and triggers](docs/screenshot.png)

Stick deadzones, response curves, and trigger ranges for Rocket League on
Omarchy, in the spirit of DS4Windows. Deadzone takes over the real
controller and hands the game a virtual Xbox 360 pad with your settings
applied. Rumble is passed back to the controller.

## Rocket League

- **Remap only while Rocket League runs**: outside the game the controller
  works as normal; Deadzone takes over the moment `RocketLeague.exe` starts.
- **Keep running without the window**: a systemd user service that starts at
  login. Together with the switch above, you never open Deadzone before
  playing.
- **Your in-game settings**: type in Controller Deadzone, Dodge Deadzone, and
  Steering and Aerial Sensitivity from the game, and Deadzone shows how they
  combine with yours: whether the two deadzones stack (with a button to
  cancel it), how far you push before flips go directional (a blue ring on
  the left stick), and where full steer and full aerial control arrive.
- **Trigger deadzone**: Rocket League adds its own deadzone to the triggers
  in `TAInput.ini`. Deadzone reads it from the game's Proton prefix and
  offers to cancel it with trigger anti-deadzone.
- **Session check**: play for 2, 5, or 10 minutes while Deadzone watches the
  controller. It shows heatmaps of where each stick spent its time and
  suggests changes with their reasons (drift at rest, reach in each
  direction, diagonal use, small versus full-tilt movement, trigger depth),
  each with an Apply button. It sees the controller, not the game.
- **Profiles** start from a preset: Standard, Freestyle (finer aerial
  control), or Worn stick (bigger deadzone, lower outer edge).

## OBS overlay

Turn on **OBS overlay** in the window, copy the address, and add it in OBS as
a Browser Source (Sources, +, Browser). It shows the sticks, triggers, and
buttons as the game receives them, on a transparent background, and keeps
working with the window closed when Deadzone runs in the background. It is
served on this computer only.

Options go after the `?` in the address; the window builds it for you:

| Option | Effect |
| --- | --- |
| `layout=compact` | Sticks and triggers only (440 by 270) instead of the whole controller (600 by 390). |
| `raw=1` | Also show the physical stick as a faint dot. |
| `profile=1` | Show the profile name underneath. |
| `buttons=xbox` | A, B, X, Y instead of the PlayStation shapes. |
| `accent=a855f7` | Color for pressed buttons and the stick dots. |
| `scale=1.5` | Size. |
| `trail=1` | A fading trail behind each stick for the last half second. |
| `history=1` | A strip of the last three seconds of button and trigger presses underneath (adds 100 to the height). |
| `demo=1` | Moves on its own, for placing it in OBS without a controller. |

## Input recording

**Record inputs** in the overlay card saves what the controller does, 60
times a second, to `~/Videos/deadzone inputs/`. **Play** opens the replay
in a browser with pause, a scrub bar, frame stepping, and quarter and half
speed; **Copy for OBS** gives a Browser Source address that plays it on
repeat without controls, to lay over the clip in an editor.

## Buttons

Each profile can remap any button to another, to nothing, or to a full
trigger press (boost on R1, for example). The overlay and the flip
analyzer follow the remapping, since they show what the game receives.

## On the controller

- **PS + d-pad left or right** switches to the previous or next profile, even
  mid-match. The lightbar flashes the new profile's color and the window and
  overlay show its name. A PS press on its own still reaches the game.
- **Lightbar** modes: off, one color, rainbow, battery level, or the current
  profile's color. A red flash and a desktop notification warn at 15%
  battery, and the window estimates the time left from how fast it drains.
- **Rumble** strength, from off to full.
- The window shows the connection and the **report rate** the controller
  actually delivers while moving.

## Practice and health

- **Precision trainer** (`T`): Flick, to targets near the rim in all eight
  directions from a released stick, like setting up flips and wavedashes; or
  Control, small targets to land on and hold. It times each target, counts
  overshoots, names your slowest direction, and keeps your results. It reads
  what the game receives, so it tests your settings as well as your hands.
- **Progress** charts your flip gap, diagonal accuracy, half-flip and speed
  flip cancels, and trainer times by day, per controller.
- **Flip analyzer**: every two jump presses within a second and a half are
  timed to the millisecond from the controller's own timestamps, with where
  the left stick was: front, back, side, or diagonal flip, or a double jump
  inside your Dodge Deadzone. It shows how far off the diagonal each diagonal
  flip was, how full each axis got, and points out flips that sit right on
  the dodge threshold. A back flip followed by a forward push counts as a
  half-flip cancel, and a forward diagonal followed by a pull back as a speed
  flip cancel, each timed. The jump button can be changed.
- **Controller health**: every session check and Measure drift adds a point
  to a per-controller chart of stick drift, with a line telling you when a
  stick's drift is rising.
- **Export and Import** turn a profile into a `DZ1-` code you can paste to a
  friend or keep as a backup.

## What you can set

Each setting explains itself behind its info icon (`I` shows them all).

| Setting | What it does |
| --- | --- |
| Shape | **Circle**, **Square**, **Cross** (each axis alone), or **Custom**: drag eight handles to set how far the deadzone reaches in each direction. |
| Separate horizontal and vertical | Its own width and height: an oval, a rectangle, or two bands. |
| Deadzone | Below this the stick reads as centered, up to 100%. Output starts from zero at the edge, so there is no jump. |
| Anti-deadzone | The smallest output once you leave the deadzone, to cancel the game's own deadzone. |
| Outer edge | Past this the stick reads as fully pushed. |
| Diagonal reach | Lets diagonals reach full travel on both axes, like an Xbox stick; the DualShock 4 otherwise stops near 71% per axis. |
| Response | Presets (Linear, Soft center, Precise, Quick, S-curve, Two-stage), one exponent, or a curve drawn point by point. Vertical can get its own curve, for pitch in the air. |
| Triggers | Deadzone, outer edge, and anti-deadzone for each. |

**Measure drift** and **Measure edge** fit the deadzone and outer edge to
your stick.

## Shortcuts

The window has four tabs: Tune, Rocket League, Practice, and Controller.

| Key | Action |
| --- | --- |
| 1 – 4 | Switch tabs |
| Space | Turn remapping on or off |
| P | Switch to the next profile |
| D | Show the next controller |
| I | Show every description under its setting |
| H | Hide the real controller from games |
| L | Next lightbar mode |
| T | Open the precision trainer |
| A | Themes, animations, particles, and your buddy |
| PS + ← → (on the controller) | Previous or next profile |
| R | Reset the profile to its preset |
| Esc | Stop measuring |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| ? | Show shortcuts |
| Q | Quit |

## Lightbar and battery

The window shows the controller's battery, and the lightbar can stay as it
is, take one color, cycle through a rainbow, or show the battery level from
green to red. Setting it needs a one-time permission, added to the same udev
rule as hiding (it asks for your password). The controller gets its own
color back when Deadzone closes. `L` cycles the lightbar mode.

## Setting up Rocket League

Linux cannot hide a controller from other programs: while Deadzone holds
the DualShock 4, Steam still lists it (silent) and makes it player 1, so
your input arrives as player 2. The setup card at the top of the window
walks through the fix and ticks off each step:

1. **Hide from games** installs `/etc/udev/rules.d/72-deadzone.rules`, which
   keeps the controller's raw HID node away from Steam and Proton.
2. **Turn off Steam Input** for Rocket League (Properties, Controller), so
   Steam neither lists the real controller nor adds its own deadzones.
3. **Launch options**: prefix them with
   `SDL_GAMECONTROLLER_IGNORE_DEVICES=0x054c/0x09cc`, so the game ignores the
   real controller. The window reads your current options and builds the
   line to paste, or writes it into Steam's settings itself while Steam is
   closed (keeping a backup of the file).
4. **Keep running without the window** and **Remap only while Rocket League
   runs**, because after step 3 the game only sees Deadzone's pad.

## Appearance

Press **A**, or the three dots at the top right, to pick a theme. **Omarchy**
follows the desktop theme's background, text, and accent, and changes with
it; the other eleven (Graphite, Midnight, Violet, Sakura, Crimson, Ember,
Forest, Lagoon, Mono, Paper, and Sand) are fixed. Two switches sit below:

- **Animations**: tabs glide in, mouse-wheel scrolling eases, choices slide,
  and themes fade into each other.
- **Particles**: soft motes drift behind the cards, sparks trail the stick
  when it snaps across, and each flip gets a burst. They pause whenever the
  window isn't focused, so they never cost the game a frame.

- **Buddy**: a pixel-art car, cat, or dog lives on top of the page. It
  wanders about, follows your left stick, hops when you jump, and flips the
  way you flip; the car boosts when you boost and turns around at the walls.
  Click it to say hi. It naps while the window is in the background.

The choices are saved in `~/.config/deadzone/appearance.json`.

## Performance

The remapper reads the controller on its own real-time thread, applies the
profile without copying it on every report, and sends only the axes that
changed, so it adds next to nothing on top of the controller's own report
interval. The window hears about stick movement as it happens and redraws
at most once per display refresh, at 240 Hz on a 240 Hz screen. Steady
sampling only runs while something records or an overlay is open, so the
background service otherwise wakes only for controller input and its slow
checks for new controllers and for Rocket League.

## Without the window

Turn on **Keep running without the window** in the Rocket League card, or
run `deadzone run` yourself. While the window is open it does the remapping
and the service waits; closing the window hands back to the service.
`deadzone list --json` shows what Deadzone can see.

## Controllers

Tested by hand with a DualShock 4 (wired and Bluetooth). The automated
device tests also drive an Xbox-style pad (the `xpad` layout and ranges) and
a DualSense-style pad through the whole remapper, using pretend controllers
made with uinput. Anything the kernel shows as a gamepad should work; the
lightbar works on the DualShock 4 and DualSense.

## Installing

From the AUR, once published: `yay -S deadzone`. From source:

```sh
git clone <this repository> && cd deadzone
bin/install
```

Deadzone needs read access to `/dev/input/event*` and write access to
`/dev/uinput`, which Steam's udev rules (or `game-devices-udev`) give the
logged-in user.

## Building

```sh
bin/build      # build/deadzone
bin/test       # unit tests, offscreen
DEADZONE_DEVICE_TESTS=1 bin/test   # also drive pretend controllers end to end (close games first)
bin/release    # source tarball and AUR PKGBUILD in dist/
bin/install    # makepkg -fsi, adds it to the app launcher
```

Deadzone needs read access to `/dev/input/event*` and write access to
`/dev/uinput`. Steam's udev rules (or `game-devices-udev`) grant both to the
logged-in user.
