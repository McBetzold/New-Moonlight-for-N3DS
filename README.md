# Moonlight for the \*New\* Nintendo 3DS

Moonlight is an open source client for [Sunshine](https://github.com/LizardByte/Sunshine) and NVIDIA GameStream for the \*New\* Nintendo 3DS, forked from [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded). Moonlight allows you to stream your full collection of games and applications from your PC to other devices to play them remotely.

## Original 3DS Note

While this app is operable on the original 3DS, the hardware decoder must be disabled and the framerate will be significantly lower due to the slower CPU.

## First time setup/use

The launcher follows the Moonlight PC and Android apps: a list of your PCs, a grid of apps with box art, and a grouped settings page. The touch screen holds the grid and a toolbar; the top screen shows details for the highlighted item plus the button hints.

### Adding and pairing a PC

After modding your 3DS and installing the Moonlight CIA, open the Moonlight application from your home menu.

- Tap `Add PC` on the bottom screen (or press `SELECT`). Choose `Search the local network` to scan your subnet for PCs running Sunshine or GeForce Experience and pick one from the list, or `Enter IP address` to type the address yourself.
    - Windows: Open the Start menu, type cmd, and press Enter. In the black window, type ipconfig and press Enter. Look for "IPv4 Address".
    - Mac: Click the Apple icon > System Settings (or Preferences) > Network. Select your Wi-Fi or Ethernet connection to see the IP address.
- The PC appears as a card. A warning badge means it is offline, a lock means it is online but not paired yet, and a play badge means an app is currently running on it.
- Select the card and press `A` (or tap it a second time). Moonlight offers to pair and shows a four-digit PIN on the top screen; enter it on the PIN page of Sunshine's web UI. The dialog closes by itself once pairing completes.
- Press `X` (or tap `Options`) on a PC for Pair/Unpair, Rename, Refresh status and Delete. `Y` refreshes the online status of every PC.

### Streaming

- Connecting to a paired PC shows its apps with their box art. The art is downloaded from the host the first time and cached under `sd:/3ds/moonlight/boxart`.
- Move with the D-pad or Circle Pad, or tap a tile. Press `A` (or tap the highlighted tile) to launch the app; if it is already running the stream resumes instead. A green badge marks the running app, and `X` (or `Quit app`) closes it on the PC.
- While the stream starts, the top screen shows the connection stages (RTSP handshake, control stream, video stream...). Once video arrives, the bottom screen shows the in-stream menu offering view/control options. Touch any of these buttons to select a streaming view.
- NOTE: You can re-open the streaming menu any time during the stream by pressing the 3DS's HOME button.
    - Pressing the HOME button again from the menu ends the stream (the app keeps running on the PC) and returns you to the 3DS HOME Menu, like the Android app does when it is sent to the background. Reopen Moonlight and press A on the app to resume.
- NOTE: The Stretch view shows the upper half of the stream on the top screen and the lower half on the bottom screen at the same scale; because the bottom screen is narrower, it shows the central 80% of the lower half rather than squeezing it. Use the `400x480 (Dual screen, 800x960 host)` preset for it: each half is then exactly the top screen's shape. With 800x480 each half is twice as wide as the screens, so the picture is stretched vertically.
- NOTE: When the Magnify view is active, you can lock the region you are magnifying by holding either of the shoulder buttons (L or R).
    - When the view is locked, you can use the touchscreen to click, drag, and release items in the selected region of the screen (similar to the Mirror and Stretch views)

### Settings

Press `START` or tap the gear icon to open the settings page. It is grouped like the PC client (Basic, Audio, Host, Input, Advanced): left/right adjusts the highlighted value, `A` toggles it or opens a keyboard for an exact value, and `B` saves and goes back. The top screen explains what the highlighted setting does and warns about stream sizes the 3DS cannot display.

## Configuration

You can modify documented settings either in the app, or by creating/modifying the config file located at `sd:/3ds/moonlight/moonlight.conf`.
The default settings are recommended for \*New\* 3DS users. The PC list is stored in `sd:/3ds/moonlight/hosts.conf`.

### PC games that refuse a tiny display

Apollo (and Sunshine with display-mode changes on) sizes the host display from the resolution in the launch request, so a plain 800x480 or 400x240 stream gives Windows and the game an 800x480 or 400x240 monitor. Many games refuse to start on that or fall back to a window. The **PC games (800x480, 1920x1152 host)** preset keeps the 800x480 stream but asks the host for a 1920x1152 display, the same 5:3 shape as the top screen; Apollo creates its virtual display at that size and scales the picture down, so the game sees a normal monitor and nothing changes on the 3DS. The request also carries `virtualDisplay=1`, so Apollo uses a virtual display even if the app is not configured for one (Sunshine ignores it). The same host size can be typed after the stream size when entering a custom resolution, and in `moonlight.conf` it is `host_width = 1920` / `host_height = 1152` (0 = same as the stream). Keep the host display the same shape as the stream, or the host adds black bars; the settings page warns when the shapes differ.

### Performance tips (New 3DS)

- 800x480 at 60fps is comfortably within the New 3DS's budget (about 6.5ms decode and 3ms present per 16.7ms frame), so there is normally no need to go lower. 800x240 (the top screen's native wide-mode size) halves the decoder's work, but only looks right if the host *scales* a 5:3 picture down to it; hosts that switch their display to the requested mode (Apollo's virtual display, Sunshine with display-mode changes on) will render an 800x240 desktop that appears stretched 2x vertically. On Apollo, add a display mode remap `800x240 -> 1280x768` before using it.
- Hardware decoding and presentation run on their own thread (core 2), so the network/FEC thread is never blocked by the decoder. Open the debug view from the in-stream menu: the bottom screen shows a status panel (fps, stream size, network RTT, decode and present times) above a scrolling log, and the leftmost column of the top screen shows per-frame decode (red) and present (blue) time with green marks at the 16.7ms frame target.
- Keep the 3DS close to the router and prefer a 2.4GHz network with little congestion; the 3DS has no 5GHz radio.

## Documentation

More information about installing and runnning Moonlight Embedded is available on the [wiki](https://github.com/moonlight-stream/moonlight-embedded/wiki).

## Build

I have included a Dockerfile which has all of the required build dependencies pre-installed. The easiest way to build moonlight.cia is by building and running the docker image.
If you are using VS Code as your editor, you can use the `Build Docker` and `Run Docker` tasks for this.
If you are not using VS Code, you can build and run the docker image with the following terminal commands from the root of the repository:

```bash
docker build --network=host -t moonlight-n3ds .
docker run --rm -it -v .:/moonlight-N3DS -w /moonlight-N3DS moonlight-n3ds:latest
```

Then, run the following command in the docker commandline:

```bash
make
```

## Install

You can download the CIA file (moonlight.cia) from the [Releases](https://github.com/zoeyjodon/moonlight-N3DS/releases/latest) page, and install it using [FBI](https://github.com/Steveice10/FBI).

Please note that to run Moonlight on the 3DS, you will need to install custom firmware. You can find instructions for installing CFW [here](https://3ds.hacks.guide/).

## See also

[Moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) is the shared codebase between different Moonlight implementations

## Contribute

1. Fork us
2. Write code
3. Send Pull Requests
