#ifndef UNIFIED_LAUNCHER_H_
#define UNIFIED_LAUNCHER_H_

// Entry point dispatch for the single-executable build: the same binary is the
// GUI and the tsMuxeR command line engine.

// True when the process should act as the command line muxer instead of
// showing the GUI.
bool unifiedShouldRunEngine(int argc, char** argv);

int unifiedRunEngine(int argc, char** argv);

#endif
