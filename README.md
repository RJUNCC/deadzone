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
| `demo=1` | Moves on its own, for placing it in OBS without a controller. |

## On the controller

- **PS + d-pad left or right** switches to the previous or next profile, even
  mid-match. The lightbar flashes the new profile's color and the window and
  overlay show its name. A PS press on its own still reaches the game.
- **Lightbar** modes: off, one color, rainbow, battery level, or the current
  profile's color. A red flash and a desktop notification warn at 15%
  battery.
- **Rumble** strength, from off to full.
- The window shows the connection and the **report rate** the controller
  actually delivers while moving.

## Practice and health

- **Precision trainer** (`T`): Flick, to targets near the rim in all eight
  directions from a released stick, like setting up flips and wavedashes; or
  Control, small targets to land on and hold. It times each target, counts
  overshoots, names your slowest direction, and keeps your results. It reads
  what the game receives, so it tests your settings as well as your hands.
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

| Key | Action |
| --- | --- |
| Space | Turn remapping on or off |
| P | Switch to the next profile |
| D | Show the next controller |
| I | Show every description under its setting |
| H | Hide the real controller from games |
| L | Next lightbar mode |
| T | Open the precision trainer |
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
   line to paste.
4. **Keep running without the window** and **Remap only while Rocket League
   runs**, because after step 3 the game only sees Deadzone's pad.

## Without the window

Turn on **Keep running without the window** in the Rocket League card, or
run `deadzone run` yourself. While the window is open it does the remapping
and the service waits; closing the window hands back to the service.
`deadzone list --json` shows what Deadzone can see.

## Building

```sh
bin/build      # build/deadzone
bin/test       # unit tests, offscreen
bin/install    # makepkg -fsi, adds it to the app launcher
```

Deadzone needs read access to `/dev/input/event*` and write access to
`/dev/uinput`. Steam's udev rules (or `game-devices-udev`) grant both to the
logged-in user.
