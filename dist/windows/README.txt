Deadzone for Windows
====================

Stick deadzones, response curves, and trigger ranges for Rocket League.
Deadzone reads your PlayStation controller and hands the game a virtual
Xbox 360 pad with your settings applied. Rumble is passed back.

Before the first run
--------------------
Deadzone needs two free drivers by Nefarius (the same ones DS4Windows uses):

  ViGEmBus  creates the virtual Xbox pad
            https://github.com/nefarius/ViGEmBus/releases/latest

  HidHide   hides the real controller from games, so they only see
            Deadzone's pad
            https://github.com/nefarius/HidHide/releases/latest

Install both, restart the computer, then run deadzone.exe from this folder.
Keep the folder together: deadzone.exe needs the files next to it.

Setting up Rocket League
------------------------
Open the Rocket League tab and follow "Set up Rocket League to use
deadzone's controller". It hides the controller through HidHide (Windows
asks for permission once) and can start Deadzone when you sign in.

To give games the real controller back, turn off "Hide the real controller
from games" at the bottom of the window.

HidHide lets deadzone.exe see the hidden controller by its location, so
if you move this folder, turn hiding off first and back on afterwards.

Command line
------------
  deadzone.exe list    controllers Deadzone can see
  deadzone.exe run     remap without the window (Ctrl+C stops it)

Settings live in %LOCALAPPDATA%\deadzone.
