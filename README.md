# Deadzone

![Deadzone shaping a controller's sticks and triggers](docs/screenshot.png)

Stick deadzones, response curves, and trigger ranges for any controller on
Omarchy, in the spirit of DS4Windows. Deadzone takes over the real controller
and hands games a virtual Xbox 360 pad with your settings applied, so every
game, Steam, and Proton get the same feel without per-game setup. Rumble from
games is passed back to the controller.

## What you can set

| Setting | What it does |
| --- | --- |
| Deadzone | Below this much travel the stick reads as centered. Output starts from zero at the edge, so there is no jump. |
| Outer edge | Past this much travel the stick reads as fully pushed, for sticks that never quite reach the rim. |
| Anti-deadzone | The smallest output once you leave the deadzone, to cancel a game's own deadzone. |
| Response curve | 1 is linear; higher values give finer aim near the center. |
| Radial / Axial | Radial treats the stick as a circle; axial treats each axis on its own, which keeps straight pushes straight. |
| Triggers | Deadzone and outer edge for each trigger. |

Settings save on their own to `~/.config/deadzone/config.json`.

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Turn remapping on or off |
| D | Show the next controller |
| H | Hide the real controller from games |
| R | Reset all settings |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| ? | Show shortcuts |
| Q | Quit |

## Hiding the real controller

Steam, SDL games, and Proton can read some controllers (DualShock 4 and
DualSense among them) through their raw HID node, which skips Deadzone and
can show two controllers in a game. **Hide from games** removes your access
to that node while remapping, using `pkexec setfacl`, so it asks for your
password. Turning it off, or quitting, gives the access back; so does
reconnecting the controller.

If Steam Input is turned on for PlayStation controllers, Steam may still
apply its own configuration on top. Turn it off under Steam > Settings >
Controller if games feel different from the preview.

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
