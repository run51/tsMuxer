# tsMuxeR Dolby Vision on a Synology NAS (Docker)

This is the same application as on Windows: tsMuxeR 2.20.0 (teaching-droid fork: dual-layer
Dolby Vision, TrueHD + AC-3 core merge, MKV output…) with this fork's additions (mux queue,
add a folder, Blu-ray ISO input, disc explorer, automatic track selection, automatic TrueHD +
AC-3 merge, output named after the source, detailed log), and MediaInfo.
It runs in a web browser at `http://<nas-ip>:5800`.

## Requirements

- A NAS with an **Intel or AMD** processor ("+" models: DS920+, DS923+, DS224+…).
  ARM models ("j" series) are not supported: the image is built for x86-64.
- **DSM 7.2** with the **Container Manager** package (Package Center).
- The image file **`tsmuxer-dv.tar`** (from the release page, ~250 MB).

If an earlier version of the image is installed, its settings are not carried over (they were
stored under another name): the interface restarts with the default settings.

## 1. Import the image

1. Copy `tsmuxer-dv.tar` to the NAS with File Station (for example into `docker`).
2. **Container Manager > Image > Action > Import > Add from file on the NAS**, choose
   `tsmuxer-dv.tar`. The image `tsmuxer-dv:latest` appears in the list.

## 2. Prepare the folders

- Create the settings folder: `docker/tsmuxer` (File Station).
- Note the share that holds your films, for example `video` → `/volume1/video`.

## 3. Your DSM user's id

Files created by tsMuxeR will belong to this user. Over SSH: `id your_user`
(the first administrator account is often `uid=1026`, group `users` = `100`).

## 4. Create the project

**Container Manager > Project > Create**

- Name: `tsmuxer`
- Path: `/docker/tsmuxer`
- Source: **Create docker-compose.yml**, paste the content of `docker-compose.yml`
  (`docker` folder of the project) and adjust:
  - `/volume1/video:/storage` → your video share,
  - `USER_ID` / `GROUP_ID` → the values from step 3,
  - `TZ` → your time zone,
  - for a French interface, uncomment `TSMUXER_LANGUAGE: "fr"` and `LANG: "fr_FR.UTF-8"`
    (read on the first start only; the language can be changed later in the window),
  - to protect access with a password, uncomment `SECURE_CONNECTION` and the
    `WEB_AUTHENTICATION...` lines: the address then becomes `https://<nas-ip>:5800` and the
    browser warns about a self-signed certificate the first time (accept it).
- Finish the wizard: the container starts.

## 5. Use

Open `http://<nas-ip>:5800` in a browser.

- The interface starts in English, with `/storage` (your share) as output folder.
- Dark mode (tsMuxeR, MediaInfo and the browser panel): `DARK_MODE: "1"` in the
  docker-compose, `"0"` for the light theme (then restart the project).
- **Always write to `/storage`** (or one of its subfolders): any other place is inside the
  container and would be lost when it is updated.
- **Add folder**: first choose the output type (ISO, Blu-ray folder, M2TS…), the output folder
  and the options, then a folder to scan. Every MKV or Blu-ray ISO found (subfolders included)
  becomes a job in the queue, with all its tracks, named after the source file (for an ISO, its
  longest playlist). Skipped and listed: films already converted (the output exists), ISOs made
  by tsMuxeR (earlier conversions), encrypted ISOs and unreadable files. Then start the queue.
- **Add file** also takes a **Blu-ray ISO image**, read directly without mounting it.
  The longest playlist (the film) is taken. If several playlists have a similar length, a list
  lets you choose. The ISO must be a decrypted copy: an encrypted (AACS) image is refused with
  a message.
- **TrueHD (Atmos)**: when an MKV holds a TrueHD track and an AC-3 track in the same language
  (preferably the one named "compatibility"), both are merged automatically, as on the original
  disc. Blu-ray players that do not decode TrueHD then play the AC-3. The merge can be changed
  or removed in the TrueHD track's options.
- **Explore a disc…** (or right-click a file from a Blu-ray, or add an `index.bdmv`): every
  playlist of an ISO or Blu-ray folder, with its duration, size, chapters, tracks and clips.
  The longest is selected; short playlists and copies are hidden. "Load this playlist" makes it
  the input; "Add selection to the queue" creates one job per playlist (episodes of a
  series), named "disc - playlist".
- **Automatic track selection** (General tab): when a file is added, only the audio and
  subtitle tracks in the chosen languages stay ticked (e.g. audio `eng, fra`, subtitles `eng`),
  with options to leave out commentaries, audio description or a word in the name.
  For French, `vff`, `vfq` or `vfi` keep only that dub (e.g. `vff, eng`), recognised from the
  `fr-FR`/`fr-CA` tag or the track's name; the language column shows it (`fra (VFQ)`).
  A track whose language is `und` takes the one its name gives ("French (Canada)").
  Also applies to "Add folder".
- **Output name**: with "Use the input file name for the output" ticked, the output takes the
  name of the source file, of the ISO, or of the disc folder for a Blu-ray folder; **Browse**
  then asks only for the output folder.
- **Estimated size** (right of the output types): from the ticked tracks, with the disc it
  fits on (BD-25, 50, 66 or 100). Available for MKV sources (mkvmerge, MakeMKV…).
- **The queue is kept** in `docker/tsmuxer`: after a restart of the container or the NAS, the
  pending jobs come back (an interrupted job starts over). Just start the queue again.
- Right-click an input file → **Open in MediaInfo**. MediaInfo opens full screen; close it with
  **File > Exit** to get back to tsMuxeR (MediaInfo's language is set in
  *Options > Preferences*).
- The panel that opens with the tab on the left of the screen gives access to the clipboard
  (copy/paste a path) and to full screen.
- Muxing carries on if you close the browser tab; open the page again to follow the queue.

## Update

Import the new `tsmuxer-dv.tar` (step 1), then **Project > tsmuxer > Action > Build** (or Stop
then Start). The settings in `docker/tsmuxer` are kept.

## Build the image (on a PC)

```
docker build -f docker/Dockerfile -t tsmuxer-dv:latest .
docker save tsmuxer-dv:latest -o dist/tsmuxer-dv.tar
```
