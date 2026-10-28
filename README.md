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
- Press `X` (or tap `Options`) on a PC for Connect, Resume running app, Pair/Unpair (plus *Pair with one-time PIN* on Apollo), Rename, Refresh status, Delete, *Wake PC (experimental)* and the *Wake-on-LAN setup guide*. `Y` refreshes the online status of every PC. A name set with Rename is kept; renaming to an empty name goes back to the PC's own name.

### Streaming

- Connecting to a paired PC shows its apps with their box art. The art is downloaded from the host the first time and cached under `sd:/3ds/moonlight/boxart`.
- Move with the D-pad or Circle Pad, or tap a tile. Press `A` (or tap the highlighted tile) to launch the app; if it is already running the stream resumes instead. A green badge marks the running app, and `X` (or `Quit app`) closes it on the PC.
- While the stream starts, a dialog shows the connection stages (RTSP handshake, control stream, video stream...). Once video arrives, the bottom screen shows the in-stream menu offering view/control options. Touch any of these buttons to select a streaming view.
- NOTE: You can re-open the streaming menu any time during the stream by pressing the 3DS's HOME button.
    - Pressing the HOME button again from the menu ends the stream (the app keeps running on the PC unless *Quit app after stream* is on) and returns you to the 3DS HOME Menu, like the Android app does when it is sent to the background. Reopen Moonlight and press A on the app to resume.
- NOTE: The Stretch view shows the upper half of the stream on the top screen and the lower half on the bottom screen at the same scale; because the bottom screen is narrower, it shows the central 80% of the lower half rather than squeezing it. Use the 800x480 preset for it.
- NOTE: When the Magnify view is active, you can lock the region you are magnifying by holding either of the shoulder buttons (L or R).
    - When the view is locked, you can use the touchscreen to click, drag, and release items in the selected region of the screen (similar to the Mirror and Stretch views)
- The in-stream menu is grouped by purpose. Top row: on-screen controls (`GAMEPAD`, `KEYBOARD`, `MOUSE` touchpad). Second row: the stream on the touch screen, with taps sent as mouse clicks (`MIRROR`) or as real touch (`TOUCH`). Third row: the other views (`STRETCH`, `MAGNIFY`). Bottom row: `EXIT`, `COMMANDS` and `DEBUG`.
- NOTE: The Touch view is the Mirror view with real touch input: Sunshine and Apollo receive the stylus as a touch screen, so Windows shows touch feedback and apps that distinguish touch from mouse behave accordingly. On hosts without touch support it falls back to mouse clicks.
- NOTE: The Commands view lists the server commands of an Apollo host (see *Apollo hosts* below).
- Holding `START` for about a second turns the C-stick into a mouse pointer, with whatever is mapped to LT/RT (`ZL`/`ZR` by default) as its buttons; a pointer icon appears in the top-left corner of the top screen while it is on. Hold `START` again to get the right stick back. This can be turned off under Input Settings (*C-stick mouse (hold START)*).
- **Auto-sprint (L3)** (Input Settings, off by default) holds L3 while the Circle Pad is pushed nearly all the way, so shooters sprint without a stick click, which the Circle Pad does not have. Easing off below about 70% releases it, so partial pushes still walk.
- A yellow warning sign in the top-right corner of the top screen means frames are being lost or delayed on the network; move closer to the router before the stream drops. If it drops anyway, Moonlight reconnects by itself up to three times (Host Settings, *Reconnect automatically*).

### Waking and resuming

- **Wake PC (experimental, untested).** Once Moonlight has connected to a paired PC, it remembers the PC's network address. When that PC later shows as offline, *Wake PC (experimental)* in its options menu (`X`) sends Wake-on-LAN packets and waits up to 90 seconds for it to answer. The PC must be set up for it first; the *Wake-on-LAN setup guide* in the same menu walks through it, and in short:
    1. Connect the PC to the router with an Ethernet cable; waking over Wi-Fi is rarely supported.
    2. In the BIOS/UEFI (Delete, F2 or F10 at power-on), enable *Wake on LAN*, *Power On By PCI-E* or *Resume by PCI-E Device*, and disable *ErP*/*EuP*/*Deep Sleep*.
    3. In Windows Device Manager, open the Ethernet adapter's properties. On *Power Management*, tick *Allow this device to wake the computer* and *Only allow a magic packet to wake the computer*; on *Advanced*, set *Wake on Magic Packet* to Enabled.
    4. Put the PC to sleep rather than shutting it down. To wake from shutdown as well, turn off Fast Startup (Control Panel → Power Options → *Choose what the power buttons do*).
- **Resume running app.** A PC with an app still running offers *Resume running app* in its options menu, which opens the app list and resumes that app in one step.
- **Resume on start** (Host Settings) does the same automatically when Moonlight opens: after the first status check, a three-second countdown resumes the running app unless you choose to stay in the launcher.
- **Volume boost** (Audio Settings) makes the stream louder than the PC sends it, up to 400%. The 3DS speakers are quiet; **keep it at 200% or below**, since higher settings clip loud passages and distort.
- **Remote Input** (Apollo's input-only mode) shows a status panel on the top screen while connected: the PC's name, a running session timer with a blinking light, and the controls. No picture or sound is streamed in this mode.

### Settings

Press `START` or tap the gear icon to open the settings page. It is grouped like the PC client (Basic, 3D Display, Audio, Host, Input, Button Mapping, Gamepad View, Advanced): left/right adjusts the highlighted value, `A` toggles it or opens a keyboard for an exact value, and `B` saves and goes back. The top screen explains what the highlighted setting does and warns about stream sizes the 3DS cannot display.

### Button mapping

The PC sees the 3DS as an Xbox-style controller. Under **Button Mapping** in the settings:

- **Layout** picks a preset. *Standard* sends the button with the same name (A sends A, L sends LB, ZL sends LT). *Xbox positions* swaps A/B and X/Y so the buttons line up with an Xbox pad's layout. *Shoulders as triggers* makes L/R the triggers and ZL/ZR the bumpers. The last preset combines both. Changing any single button shows the layout as *Custom*.
- **A, B, X, Y, L, R, ZL, ZR, START and SELECT** can each send any controller button: face buttons, bumpers, triggers, View, Menu, HOME (the Xbox Guide button), Capture, either stick click (or both at once), a D-pad direction, or nothing. The D-pad and both sticks are fixed.
- **Gamepad View** assigns the five boxes of the bottom-screen gamepad view (three across the top, two side by side below). By default they send L3, L3 + R3, R3, HOME and Capture. Capture presses View and Menu together: Apollo's virtual Xbox and DualShock 4 pads have no Capture button, and SwitchBridge turns that pair into the Switch's Capture button by default. The view's picture is drawn from the mapping, so its labels always show what each box sends.
- **Reset all mapping** restores the defaults.

In `moonlight.conf` the mapping is two lines, for example `button_map = a=b,b=a,x=y,y=x,l=lb,r=rb,zl=lt,zr=rt,start=menu,select=view` and `touch_map = topleft=l3,topmiddle=l3r3,topright=r3,bottomleft=guide,bottomright=capture`. The target names are `a b x y lb rb lt rt view menu guide l3 r3 l3r3 up down left right capture none` (`guide` is HOME). An older `bottom=` entry sets the bottom-left box. The older `swapfacebuttons` and `swaptriggersandshoulders` options are converted into a mapping the first time settings are saved.

## Configuration

You can modify documented settings either in the app, or by creating/modifying the config file located at `sd:/3ds/moonlight/moonlight.conf`.
The default settings are recommended for \*New\* 3DS users. The PC list is stored in `sd:/3ds/moonlight/hosts.conf`.

### Apollo hosts

[Apollo](https://github.com/ClassicOldSong/Apollo) is a Sunshine fork with a built-in virtual display and a few extras of its own. Moonlight for 3DS uses them when it sees an Apollo host; Sunshine and GeForce Experience hosts are unaffected.

- **Client name.** The 3DS pairs as `New 3DS` (or `3DS` on an original model) instead of moonlight-embedded's old placeholder `roth`, so it is easy to find in Apollo's client list, where per-client settings such as the display mode override and permissions live.
- **Permissions.** Apollo gives every client after the first only *view streams* and *list apps*. The PC card and the app list say so, and the launcher explains what to enable, instead of failing with a bare "Permission denied". Grant permissions on the PIN tab of Apollo's web UI.
- **One-time PIN pairing.** Apollo's PIN tab can generate a PIN and a passphrase; choose `Pair with one-time PIN` from the PC's options menu and type them on the 3DS. Nobody has to type anything on the PC. The normal pairing flow still works too.
- **Server commands.** Commands the host admin defines under *Server Commands* in Apollo's web UI appear in the stream menu under `COMMANDS` when this client has the *Server Command* permission; tap one to run it on the PC. A command that toggles a ReShade preset, for example, switches 3D on and off without touching the PC.
- **App order and identity.** The app grid follows the order set in Apollo's web UI, and apps are launched by their UUID as well as their ID.
- **Input only.** With *Input only mode* enabled in Apollo, a `Remote Input` entry appears in the app list. Streaming it sends no video: the 3DS just acts as a controller, touchpad and keyboard for the PC.

### PC games that won't run below HD

Apollo (and Sunshine with display-mode changes on) sizes the host display from the resolution in the launch request, so a plain 800x480 or 400x240 stream gives Windows and the game a monitor that small. Many games refuse to start below HD or fall back to a window. The **800x480 (PC games, experimental)** preset exists for them: it keeps the 800x480 stream but asks the host for a 1920x1152 display, the same 5:3 shape as the top screen. Apollo creates its virtual display at that size and scales every frame down, so the game sees an ordinary HD monitor and nothing changes on the 3DS. The request also carries `virtualDisplay=1`, so Apollo uses a virtual display even if the app is not configured for one (Sunshine ignores it). The same host size can be typed after the stream size when entering a custom resolution, and in `moonlight.conf` it is `host_width = 1920` / `host_height = 1152` (0 = same as the stream). Keep the host display the same shape as the stream, or the host adds black bars; the settings page warns when the shapes differ.

### 3D

The top screen can show the stream in stereoscopic 3D. Choose a mode under **3D Display** in the settings (`stereo_3d` in `moonlight.conf`), start the stream and raise the 3D slider; lowering it all the way returns to 2D. With the default, `off`, the slider does nothing. Earlier versions split any 800-wide stream in two whenever the slider was up, which garbled ordinary 2D streams. The **3D setup guide** entry under 3D Display walks through the PC side on the 3DS itself: games, emulators, videos and how to check the picture.

- **Side-by-side** (`stereo_3d = sbs`) shows real 3D. The PC has to output *half* side-by-side 3D: the left-eye view squeezed into the left half of the picture and the right-eye view into the right half. Moonlight sends each half to its eye. Ways to get that picture on the PC:
    - [ReShade](https://reshade.me) with the SuperDepth3D shader in its side-by-side mode, which works for most DirectX, OpenGL and Vulkan games that expose their depth buffer.
    - geo-11, which renders true two-camera 3D in supported games.
    - Emulators with a side-by-side stereoscopic output, such as Dolphin and RPCS3.
    - Side-by-side 3D videos played full screen.

  Stream at 800x480 (the Desktop or PC games preset): each eye then gets a full 400x240 picture with the right shape. A 400-wide stream works too, but each eye only has 200 columns.

  **Getting the PC to output it.** Whatever draws the picture has to render two views and pack them into one frame the size of the screen Apollo streams: the left eye squeezed into the left half, the right eye into the right half. Look for a "3D", "stereoscopic" or "SBS" option in the game, emulator or video player; when there is none, ReShade's SuperDepth3D adds one to most games. Choose *half* side-by-side when offered, and run it fullscreen on Apollo's virtual display rather than in a window on another monitor. To check before raising the slider, look at the stream in 2D: it should show two nearly identical copies of the scene side by side, each squeezed to half its width. Copies with normal proportions mean full-width side-by-side, which comes out squashed, and copies stacked top and bottom are a different 3D format that Moonlight cannot use.
- **Flat depth** (`stereo_3d = depth`) works with any stream. Both eyes see the whole picture, slightly offset so it sits behind the screen like a window; the slider sets how far. It does not add depth within the picture.
- **Swap eyes** (`swap_eyes = true`) gives each eye the other eye's picture. Turn it on if side-by-side 3D looks inside out, with near things appearing far away. In flat depth mode it brings the picture out in front of the screen instead.

### Performance tips (New 3DS)

- 800x480 at 60fps is comfortably within the New 3DS's budget (about 6.5ms decode and 3ms present per 16.7ms frame), so there is normally no need to go lower. 800x240 (the top screen's native wide-mode size) halves the decoder's work, but only looks right if the host *scales* a 5:3 picture down to it; hosts that switch their display to the requested mode (Apollo's virtual display, Sunshine with display-mode changes on) will render an 800x240 desktop that appears stretched 2x vertically. On Apollo, add a display mode remap `800x240 -> 1280x768` before using it.
- Hardware decoding and presentation run on their own thread (core 2), so the network/FEC thread is never blocked by the decoder. Open the debug view from the in-stream menu: the bottom screen shows a status panel above a scrolling log with the frame rate, stream size, frames lost on the network since the last update, control-channel RTT, decode and present times, and the host's own capture-to-send latency. Frames lost with a low host latency point at Wi-Fi; a high host latency points at the PC.
- Keep the 3DS close to the router and prefer a 2.4GHz network with little congestion; the 3DS has no 5GHz radio.

## Documentation

More information about installing and runnning Moonlight Embedded is available on the [wiki](https://github.com/moonlight-stream/moonlight-embedded/wiki).

## Build

I have included a Dockerfile which has all of the required build dependencies pre-installed. The easiest way to build moonlight.cia is by building and running the docker image.
If you are using VS Code as your editor, you can use the `Build Docker` and `Run Docker` tasks for this.
If you are not using VS Code, you can build and run the docker image with the following terminal commands from the root of the repository:

```bash
sh 3ds/apply-patches.sh
docker build --network=host -t moonlight-n3ds .
docker run --rm -it -v .:/moonlight-N3DS -w /moonlight-N3DS moonlight-n3ds:latest
```

The first line applies the small patches in `patches/` to the git submodules (moonlight-common-c gains Apollo's server-command packet). It only needs to run once after cloning and is safe to repeat; the GitHub Actions workflow runs it too.

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
