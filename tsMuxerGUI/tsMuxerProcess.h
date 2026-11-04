#ifndef TSMUXER_PROCESS_H_
#define TSMUXER_PROCESS_H_

#include <QStringList>

// Location of the muxer engine: this executable itself in the unified build,
// otherwise a tsmuxer/tsMuxeR binary next to the GUI.
QString tsMuxerBinaryPath();

// Command line for the engine, including the marker the unified build needs.
QStringList tsMuxerEngineArgs(const QStringList& args);

#endif
