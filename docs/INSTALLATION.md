# tsMuxer: Installation Instructions

This fork has two builds, both on the [releases](https://github.com/run51/tsMuxer/releases) page:

- `tsMuxeR.exe`: Windows, 64-bit.
- `tsmuxer-dv.tar`: a Docker image, for a NAS, a server or Docker Desktop.

There is no native Linux or macOS program. On Linux, use the Docker image. The original project,
[teaching-droid/tsMuxer](https://github.com/teaching-droid/tsMuxer/releases), publishes Linux and
macOS builds, without this fork's additions.

## Windows

`tsMuxeR.exe` is a single file holding the engine and the interface, with no DLLs beside it and
nothing to install. Save it anywhere and double-click it to open the interface. Run from a command
line with arguments, it works as the tsMuxeR engine; `tsMuxeR.exe --gui` opens the interface.

It needs Windows 10 or 11, 64-bit: it is built with MSYS2 and Qt 6.12, linked statically, which
do not run on Windows 7 or 8.

The settings are kept in the registry (`HKEY_CURRENT_USER\Software\tsMuxeR\tsMuxeR`). For a
portable copy that keeps them beside the program instead, create an empty `tsMuxerGUI.ini` file
next to `tsMuxeR.exe`.

MediaInfo is not included in the Windows build. To use **Open in MediaInfo**, install
[MediaInfo](https://mediaarea.net/en/MediaInfo); the first time, tsMuxeR asks where it is if it
does not find it.

### If your antivirus flags it

Some products report Windows binaries like this one as something like `Win64:Evo-gen`. A name of
that shape is a generic heuristic. It says the file resembles a statistical pattern the engine has
learned, not that anything known was found inside it. A program that is not code signed and writes
very large files is close enough to that pattern to trip it.

The binary is not code signed. What you can check for yourself:

- Scan it with more than one engine. A single generic name from one engine, while the others stay
  quiet, is weak evidence.
- Build it yourself from this repository and compare. See [COMPILING.md](COMPILING.md); the
  single-executable Windows build is made with `scripts/build_unified_msys2.sh`.

## Docker

See [docker/INSTALL_DOCKER.md](../docker/INSTALL_DOCKER.md): `docker run`, Docker Compose, the
options, and the steps for Synology Container Manager. The image is built for x86-64 machines.
