# JkFlow 1.00 player guide

JkFlow runs your own copy of Blur (PC) on a PS4. It is an unofficial fan
project. Blur is a trademark of Activision and is not included: you need
your own copy.

## Before you start

- A PS4 with GoldHEN (the homebrew enabler) running. JkFlow is tested on a
  PS4 Slim.
- A way to copy files to the PS4: FTP (GoldHEN has an FTP server), or a
  USB drive and a file manager app on the PS4. The game must end up in
  /data/JkFlow/Blur; JkFlow does not run it from the USB drive.
- Your own Blur (PC) folder.

## Install

1. Install the JkFlow package (.pkg) on the PS4 and open JkFlow once.
2. Copy all your Blur files over FTP into this folder:

```
/data/JkFlow/Blur
```

   It should look like this:

```
/data/JkFlow/Blur/Blur.exe
/data/JkFlow/Blur/binkw32.dll
/data/JkFlow/Blur/...  (all other Blur files and folders)
```

   Blur.exe must be directly in this folder, not in a folder inside it. Upper
   or lower case does not matter. Only use a copy of Blur you own.
3. In JkFlow, open **Patch** and choose:
   - **Patch for offline play**: installs only the JkFlow files. Your own
     `Blur.exe` stays. No internet needed.
   - **Patch for online play (AMAX)**: also downloads the AMAX online client and
     Blur 1.2 from amax-emu.com and installs them. The AMAX files are not part
     of JkFlow. Without internet, see Help on the Patch screen.
4. Press **Play**.

## Frame rate

Choose it in **Settings > Frame rate**:

- **30 FPS** (default, recommended): races run at a steady 30 FPS, menus at
  60. Recommended for PS4 and PS4 Slim.
- **60 FPS**: races run at up to 60 FPS; split screen stays at 30. For PS4 Pro
  with Boost Mode on (PS4 Settings > System > Boost Mode). It may also work on
  other models, but races can be less stable.

A `Blur.exe` that is not a verified Blur always runs at 30 FPS, menus
included. Run **Patch for online play (AMAX)** and follow the steps; after
that you can turn on 60 FPS on a PS4 Pro.

## Online play

Online play uses the AMAX community servers and needs an AMAX account. How
to get one: amax-emu.com/how_to_play (official AMAX page). Then enter it in
**Settings > Online account**.

## Your data

Everything JkFlow keeps is in `/data/JkFlow` on the PS4: your Blur folder,
the launcher's files (`System`), and logs and support bundles (`Logs`).

- Uninstalling, reinstalling or updating the JkFlow package does not touch
  `/data/JkFlow`: your Blur folder, saves and settings stay.
- Deleting `/data/JkFlow` deletes your saves, your AMAX account and settings too. Copy your save
  to a computer first (Help > Saves in JkFlow says where it is).

## Help and problems

- Help (Square on the Start or Patch screen) explains every step.
- **Settings > Save support bundle** collects the logs and your settings into one
  folder. If needed, copy it to a PC over FTP. Your AMAX account, user name
  and LAN addresses are not included.
- **Settings > Enable logs** writes detailed logs while you play. Leave it off
  unless something goes wrong.

## Support

JkFlow has no contact address. If something goes wrong: read the FAQ in the
README of the source repository (github.com/Salru13/JkFlow), make a support
bundle (above) and share its files with whoever helps you. Never share your
AMAX password.
