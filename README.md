# Deadzone

![Deadzone shaping a controller's sticks and triggers](docs/screenshot.png)

Stick deadzones, response curves, and trigger ranges for any controller on
Omarchy, in the spirit of DS4Windows. Deadzone takes over the real controller
and hands games a virtual Xbox 360 pad with your settings applied, so every
game, Steam, and Proton get the same feel without per-game setup. Rumble from
games is passed back to the controller.

## What you can set

Every setting has a short description under it in the window (press `I` to
hide them).

| Setting | What it does |
| --- | --- |
| Shape | **Circle**: every direction needs the same push to leave the deadzone. **Square**: reaches further toward the diagonals. **Cross**: each axis has its own deadzone, so straight pushes stay straight. |
| Deadzone | Below this the stick reads as centered. Output starts from zero at the edge, so there is no jump. |
| Outer edge | Past this the stick reads as fully pushed, for sticks that never quite reach the rim. |
| Anti-deadzone | The smallest output once you leave the deadzone, to cancel a game's own deadzone. |
| Square output | Lets diagonals reach full travel on both axes, like an Xbox stick. Round-gated sticks such as the DualShock 4 otherwise stop near 71% per axis at a diagonal. |
| Response | **Curve**: one exponent, 1 is linear. **Custom**: drag points on the graph to draw your own. |
| Triggers | Deadzone, outer edge, and anti-deadzone for each trigger. |

**Measure drift** (let go for three seconds) and **Measure edge** (roll the
stick around the rim for five) fit the deadzone and outer edge to your stick.

## Profiles

Each profile is a full set of settings. New profiles start from a preset
(Default, Rocket League, Shooter, Racing), and each preset comes with tips for
setting up its game. Give a profile part of a game's program name, like
`RocketLeague.exe`, and it switches in while that game runs and switches back
when it closes. Settings save on their own to `~/.config/deadzone/config.json`.

The Rocket League preset uses a small circular deadzone, square output on the
left stick so a diagonal gives full steer and full pitch at once, and a 10%
trigger anti-deadzone to cancel the one Rocket League adds to the triggers on
its own.

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Turn remapping on or off |
| P | Switch to the next profile |
| D | Show the next controller |
| I | Show or hide descriptions |
| H | Hide the real controller from games |
| R | Reset the profile to its preset |
| Esc | Stop measuring |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| ? | Show shortcuts |
| Q | Quit |

## Hiding the real controller

Steam and Proton read DualShock 4 and DualSense controllers through their raw
HID node, which skips Deadzone, so a game sees two controllers: Start on the
second one joins split screen. **Hide from games** installs
`/etc/udev/rules.d/72-deadzone.rules` (it asks for your password), which keeps
that node away from your user now and on every reconnect. Turn the
controller off and on once afterwards so programs that already opened it let
go. With Deadzone closed, games still get the controller the ordinary way.
Turning the switch off removes the rule.

## Without the window

Remapping stops when the window closes. To keep it running in the
background, with the settings saved from the window:

```sh
deadzone run                                # until Ctrl+C
systemctl --user enable --now deadzone      # at every login
```

Close the window before starting the service: only one of them can hold a
controller at a time. `deadzone list --json` shows what Deadzone can see.

## Building

```sh
bin/build      # build/deadzone
bin/test       # unit tests, offscreen
bin/install    # makepkg -fsi, adds it to the app launcher
```

Deadzone needs read access to `/dev/input/event*` and write access to
`/dev/uinput`. Steam's udev rules (or `game-devices-udev`) grant both to the
logged-in user.
