# tsMuxeR Dolby Vision in Docker

This is the same application as on Windows: tsMuxeR 2.20.0 (teaching-droid fork: dual-layer
Dolby Vision, TrueHD + AC-3 core merge, MKV output…) with this fork's additions (mux queue,
add a folder, Blu-ray ISO input, disc explorer, automatic track selection, automatic TrueHD +
AC-3 merge, output named after the source, detailed log), and MediaInfo.
It runs in a web browser at `http://<host>:5800`.

## Requirements

- A host with an **x86-64** (Intel or AMD) processor running Docker: a Linux server, Docker
  Desktop on Windows or macOS, or a NAS (Synology "+" models, QNAP, Unraid…).
  ARM hosts (Raspberry Pi, Synology "j" series, Apple silicon without emulation) are not
  supported: the image is built for x86-64.
- The image file **`tsmuxer-dv.tar`** (from the release page, ~250 MB), or build it yourself
  (see the end of this guide).

## Folders

The container uses two folders of the host:

- `/config`: tsMuxeR's and MediaInfo's settings and the mux queue. Create an empty folder for it.
- `/storage`: your films. tsMuxeR reads its sources and writes its outputs there.

Files created by tsMuxeR belong to the user given by `USER_ID` and `GROUP_ID`: use the ids of
the host user who owns the films (`id <user>` on Linux; on a Synology NAS the first
administrator is often `1026`, group `users` = `100`).

## Install with docker run

```
docker load -i tsmuxer-dv.tar
docker run -d --name tsmuxer -p 5800:5800 \
  -v /path/to/tsmuxer-config:/config \
  -v /path/to/videos:/storage \
  -e USER_ID=1000 -e GROUP_ID=1000 \
  -e TZ=Europe/London \
  --restart unless-stopped \
  tsmuxer-dv:latest
```

Then open `http://<host>:5800`.

## Install with Docker Compose

`docker-compose.yml` (in the `docker` folder of the project) holds the same settings and all the
options. Adjust its paths, `USER_ID`/`GROUP_ID` and `TZ`, then:

```
docker load -i tsmuxer-dv.tar
docker compose up -d
```

## Options

Set in the `environment:` section of the docker-compose, or with `-e NAME=value` for docker run.

| Option | Effect |
|---|---|
| `TSMUXER_LANGUAGE: "fr"` | French interface. Read on the first start only (empty `/config`); the language can be changed later in the window. English by default. |
| `LANG: "fr_FR.UTF-8"` | French number formats. |
| `DARK_MODE: "1"` | Dark theme for tsMuxeR, MediaInfo and the browser panel (`"0"` = light). |
| `DISPLAY_WIDTH`, `DISPLAY_HEIGHT` | Size of the desktop shown in the browser. |
| `KEEP_APP_RUNNING: "1"` | Restart tsMuxeR if its window is closed. |
| `SECURE_CONNECTION: "1"` with `WEB_AUTHENTICATION: "1"`, `WEB_AUTHENTICATION_USERNAME`, `WEB_AUTHENTICATION_PASSWORD` | A login page for the web interface. The address becomes `https://<host>:5800`, and the browser warns about a self-signed certificate the first time (accept it). Recommended on a network others use. |

## Synology NAS (Container Manager)

DSM 7.2 with the **Container Manager** package (Package Center).

1. **Import the image**: copy `tsmuxer-dv.tar` to the NAS with File Station (for example into
   `docker`), then **Container Manager > Image > Action > Import > Add from file on the NAS**
   and choose it. The image `tsmuxer-dv:latest` appears in the list.
2. **Folders**: create `docker/tsmuxer` (File Station) for the settings, and note the share
   that holds your films, for example `video` → `/volume1/video`.
3. **Create the project**: **Container Manager > Project > Create**, name `tsmuxer`, path
   `/docker/tsmuxer`, source **Create docker-compose.yml**. Paste `docker-compose.yml` and adjust
   `/volume1/video:/storage`, `USER_ID`/`GROUP_ID` (over SSH: `id your_user`) and `TZ`.
   Finish the wizard: the container starts.
4. **Update**: import the new `tsmuxer-dv.tar` (step 1), then **Project > tsmuxer > Action >
   Build** (or Stop then Start). The settings in `docker/tsmuxer` are kept.

## Use

Open `http://<host>:5800` in a browser.

- The interface starts with `/storage` as output folder.
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
  the input; "Add selection to the queue" creates one job per playlist (episodes of a series),
  named "disc - playlist".
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
- **The queue is kept** in `/config`: after a restart of the container or the host, the pending
  jobs come back (an interrupted job starts over). Just start the queue again.
- Right-click an input file → **Open in MediaInfo**. MediaInfo opens full screen; close it with
  **File > Exit** to get back to tsMuxeR (MediaInfo's language is set in
  *Options > Preferences*).
- The panel that opens with the tab on the left of the screen gives access to the clipboard
  (copy/paste a path) and to full screen.
- Muxing carries on if you close the browser tab; open the page again to follow the queue.

If an earlier version of the image was used, its settings are not carried over (they were stored
under another name): the interface restarts with the default settings.

## Build the image

From the root of the project, on any x86-64 machine with Docker:

```
docker build -f docker/Dockerfile -t tsmuxer-dv:latest .
docker save tsmuxer-dv:latest -o dist/tsmuxer-dv.tar
```
