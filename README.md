# Sports Timer (Pebble)

A countdown for quarters of 17:30, with home and away scores. It is for black-and-white Pebbles (aplite: Pebble/Steel, diorite: Pebble 2, flint: Pebble 2 Duo).

It is a watch**app**, not a watchface: a watchface cannot receive button presses. Start it from the app menu.

## Buttons

| Button | Click | Double-click | Hold |
|---|---|---|---|
| Up | home +1 | home −1 | home = 0 (1.5 s) |
| Select | start / pause; when the alarm is on: stop the alarm | – | reset the time; when the time is already reset: open the settings |
| Down | away +1 | away −1 | away = 0 (1.5 s) |
| Up + Down | – | – | reset timer and both scores (1.5 s) |

A double-click at score 0 does nothing (the score cannot go below 0).

At 0:00 the watch vibrates. It continues to vibrate until you push Select. The time then continues as overtime (`+1:23`).
The state stays when you leave the app. A clock that runs continues to count, but it does not vibrate while the app is closed.

## Screensaver

When the timer is reset (at the start of a quarter) and no button is pushed for the set time, the image shows full screen.
The screensaver does not start while the timer runs or is paused in a quarter. Any button (also Back) only closes it.

## Settings

On the watch (hold Select when the time is reset): Timer (quarter length), Background (faint image) on/off, and
Screensaver, which opens a submenu: Screensaver on/off, Timeout, Start in saver (the app opens in the screensaver),
Show time (time of day, big, watch timezone and 12/24h setting), Time place (Middle, Bottom, Top) and Time size
(Large, Medium, Small). Rows show only when they apply.

In the Pebble phone app (gear icon of Sports Timer): the image only. Upload a photo, or clear it with **Clear photo**.
Without a photo there is no background image, and the screensaver does not start.
After you upload a photo, a preview shows it as the watch will show it. Controls: brightness, contrast,
dither method (Atkinson = sharp, Floyd-Steinberg = smooth, none), fill screen (crop) and invert.

## Build

    ./pebble.sh build          # makes build/app.pbw (the first run builds the Docker image)
    ./pebble.sh emu diorite    # emulator in an X11 window: arrow keys = Up/Select(→)/Down/Back(←)

## Install on the watch

1. Install the Pebble app on your phone (https://rePebble.com/app) and pair the watch.
2. In the app: Devices → set **Dev Connect** to on → sign in with GitHub.
3. On the computer, one time: `./pebble.sh login` (sign in with the same GitHub account). The token is kept in `~/.config/pebble-docker`.
4. `./pebble.sh build && ./pebble.sh install --cloudpebble`

Dev Connect in this app goes through the cloud. `--phone <ip>` does not work with it.
Another method: open `build/app.pbw` on the phone with the Pebble app.
