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
- NOTE: The Stretch view shows the upper half of the stream on the top screen and the lower half on the bottom screen at the same scale; because the bottom screen is narrower, it shows the central 80% of the lower half rather than squeezing it. Use the 800x480 preset for it.
- NOTE: When the Magnify view is active, you can lock the region you are magnifying by holding either of the shoulder buttons (L or R).
    - When the view is locked, you can use the touchscreen to click, drag, and release items in the selected region of the screen (similar to the Mirror and Stretch views)

### Settings

Press `START` or tap the gear icon to open the settings page. It is grouped like the PC client (Basic, Audio, Host, Input, Advanced): left/right adjusts the highlighted value, `A` toggles it or opens a keyboard for an exact value, and `B` saves and goes back. The top screen explains what the highlighted setting does and warns about stream sizes the 3DS cannot display.

## Configuration

You can modify documented settings either in the app, or by creating/modifying the config file located at `sd:/3ds/moonlight/moonlight.conf`.
The default settings are recommended for \*New\* 3DS users. The PC list is stored in `sd:/3ds/moonlight/hosts.conf`.

### Performance tips (New 3DS)

- For single-screen gaming, set `height` to `240` (keep `width` at `800`). The top screen is 800x240 in wide mode, so an 800x480 stream is only ever downscaled; 800x240 halves the decoder's work and the bitrate needed for the same picture. Use 480 if you rely on the dual-screen views.
- Hardware decoding and presentation run on their own thread (core 2), so the network/FEC thread is never blocked by the decoder. Open the debug view from the in-stream menu to see per-frame decode (red) and present (blue) time on the top scanline; the green marks are the 16.7ms frame target.
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
