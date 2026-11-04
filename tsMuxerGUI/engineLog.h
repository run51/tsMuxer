#ifndef ENGINE_LOG_H_
#define ENGINE_LOG_H_

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

// Turns the engine's English log into the readable log shown in the mux
// window and the queue: steps with elapsed time, the output stream table,
// Dolby Vision statistics, warnings explained in plain words and a final
// summary (size, duration, bitrate, speed).
class EngineLog
{
    Q_DECLARE_TR_FUNCTIONS(EngineLog)

   public:
    void start(const QString& outputName);

    // Lines to display for one engine line; empty when the line is folded into
    // the summary or repeats a warning already shown.
    QStringList format(const QString& line);

    QStringList summary(int exitCode) const;

   private:
    QString elapsed() const;
    QString step(const QString& text) const;
    QString warning(const QString& line);

    QElapsedTimer m_timer;
    QString m_outputName;
    double m_muxedSeconds = 0;
    bool m_streamHeaderShown = false;
    int m_warningCount = 0;
    QSet<QString> m_shownOnce;
};

#endif
