#include "engineLog.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

namespace
{
QString codecName(const QString& programName)
{
    static const QList<QPair<QString, QString>> names = {
        {"V_MPEGH/ISO/HEVC", "HEVC"},   {"V_MPEG4/ISO/AVC", "H.264"}, {"V_MPEG4/ISO/MVC", "MVC"},
        {"V_MPEGI/ISO/VVC", "VVC"},     {"V_MPEG-2", "MPEG-2"},       {"V_MS/VFW/WVC1", "VC-1"},
        {"A_AC3", "AC3/E-AC3"},         {"A_MLP", "TrueHD"},          {"A_DTS", "DTS"},
        {"A_LPCM", "LPCM"},             {"A_AAC", "AAC"},             {"A_MP3", "MP3"},
        {"S_HDMV/PGS", "PGS"},          {"S_TEXT/UTF8", "SRT -> PGS"},
    };
    for (const auto& n : names)
        if (n.first == programName)
            return n.second;
    return programName;
}

double timeToSeconds(const QString& text)
{
    const auto parts = text.split(':');
    double seconds = 0;
    for (const auto& p : parts) seconds = seconds * 60 + p.toDouble();
    return seconds;
}

QString formatDuration(double seconds)
{
    const int total = static_cast<int>(seconds + 0.5);
    const int h = total / 3600;
    const int m = total / 60 % 60;
    const int s = total % 60;
    if (h > 0)
        return QString("%1 h %2 min %3 s").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    if (m > 0)
        return QString("%1 min %2 s").arg(m).arg(s, 2, 10, QChar('0'));
    return QString("%1 s").arg(s);
}

qint64 outputSize(const QString& outputName)
{
    const QFileInfo fi(outputName);
    if (fi.isFile())
        return fi.size();
    qint64 size = 0;
    QDirIterator it(outputName, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        size += it.fileInfo().size();
    }
    return size;
}
}  // namespace

void EngineLog::start(const QString& outputName)
{
    m_timer.start();
    m_outputName = outputName;
    m_muxedSeconds = 0;
    m_streamHeaderShown = false;
    m_warningCount = 0;
    m_shownOnce.clear();
}

QString EngineLog::elapsed() const
{
    const qint64 s = m_timer.isValid() ? m_timer.elapsed() / 1000 : 0;
    return QString("[%1:%2]").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

QString EngineLog::step(const QString& text) const { return elapsed() + QString::fromUtf8(" ▶ ") + text; }

QString EngineLog::warning(const QString& line)
{
    QString text;
    static const QRegularExpression noElRe("no enhancement layer found in track (\\d+)");
    static const QRegularExpression noRpuRe("Dolby Vision: (\\d+) frames without RPU");
    static const QRegularExpression extraRpuRe("Dolby Vision: (\\d+) frames with more than one RPU");
    static const QRegularExpression peakRe(
        "this disc asks to be read at ([0-9.]+) Mbit/s at its fastest, and ([0-9.<]+) per cent");
    static const QRegularExpression lateRe(
        "with --maxbitrate=(\\d+), (\\d+) of (\\d+) video pictures arrive after they are due, up to ([0-9.]+) s late");
    QRegularExpressionMatch m;
    if (line.contains("MLP codec is not standard"))
        text = tr("TrueHD track without AC3 core: a Blu-ray player will not play it. Use an AC3 track instead.");
    else if (line.contains("AAC codec is not standard"))
        text = tr("AAC audio is not standard on Blu-ray: the disc may not play.");
    else if (line.contains("Dolby Vision Double Layer Single Tracks are not standard"))
        text = tr("Dolby Vision BL+EL kept in a single track: a Blu-ray player will not use it. "
                  "Check \"Split Dolby Vision layers\".");
    else if (line.contains("video height < 1080"))
        text = tr("Video height below 1080 lines: not standard for a UHD Blu-ray.");
    else if ((m = noElRe.match(line)).hasMatch())
        text = tr("Dolby Vision: no enhancement layer found in track %1.").arg(m.captured(1));
    else if ((m = noRpuRe.match(line)).hasMatch())
        text = tr("Dolby Vision: %1 frames without RPU metadata (incomplete source file?).").arg(m.captured(1));
    else if ((m = extraRpuRe.match(line)).hasMatch())
        text = tr("Dolby Vision: %1 frames with more than one RPU.").arg(m.captured(1));
    else if ((m = peakRe.match(line)).hasMatch())
        text = tr("Peak bitrate %1 Mbit/s (%2 % of the packets above the player limit): may stutter on a standalone "
                  "player. Set the maximum bitrate to 109000 kbit/s.")
                   .arg(m.captured(1), m.captured(2));
    else if ((m = lateRe.match(line)).hasMatch())
        text = tr("Bitrate limited to %1 kbit/s: %2 of %3 pictures arrive up to %4 s late, the video peaks are above "
                  "the limit. A player may stutter there; only a re-encode avoids it.")
                   .arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
    else if (line.contains("no AC-3 compatibility core was written for this TrueHD track"))
        text = tr("TrueHD track without AC3 core: a Blu-ray player will not play it. Use an AC3 track instead.");
    else
        text = line;

    if (m_shownOnce.contains(text))
        return QString();
    m_shownOnce.insert(text);
    ++m_warningCount;
    return QString::fromUtf8("⚠ ") + text;
}

QStringList EngineLog::format(const QString& line)
{
    static const QRegularExpression splitRe("^Dolby Vision: splitting track (\\d+)");
    static const QRegularExpression streamRe("^Output stream: PID (0x[0-9A-F]+) (\\S+)(?: lang=(\\S+))?(.*)$");
    static const QRegularExpression statsRe(
        "^Dolby Vision statistics: (\\d+) frames, (\\d+) RPU, (\\d+) EL NAL units, BL ([0-9.]+) MB, EL ([0-9.]+) MB");
    static const QRegularExpression durationRe("^Muxed duration: ([0-9:.]+)");
    static const QRegularExpression fpsRe("^\\w+ muxing fps is not set\\. Get fps from stream\\. Value: ([0-9.]+)");
    static const QRegularExpression decodingRe("^Decoding (.+) stream \\(track (\\d+)\\): ?(.*)$");
    static const QRegularExpression framesRe("^Processed (\\d+) video frames");

    const QString l = line.trimmed();
    QRegularExpressionMatch m;
    if (l.isEmpty() || l.startsWith("Muxing time:") || l.startsWith("Demuxing time:"))
        return {};
    if ((m = splitRe.match(l)).hasMatch())
        return {step(tr("Dolby Vision profile 7: splitting track %1 into base layer (BL) and enhancement layer "
                        "with its metadata (EL)")
                         .arg(m.captured(1)))};
    if (l == "Muxing streams")
        return {step(tr("Step: muxing streams"))};
    if ((m = streamRe.match(l)).hasMatch())
    {
        QString role = m.captured(4).trimmed();
        role.replace("(Dolby Vision enhancement layer + RPU)", tr("(Dolby Vision layer: EL + RPU metadata)"));
        role.replace("(secondary)", tr("(secondary)"));
        QStringList out;
        if (!m_streamHeaderShown)
        {
            out << tr("Output streams:");
            m_streamHeaderShown = true;
        }
        QString row = QString("   PID %1  %2  %3  %4").arg(m.captured(1), codecName(m.captured(2)), m.captured(3), role);
        while (row.endsWith(' ')) row.chop(1);
        out << row;
        return out;
    }
    if ((m = statsRe.match(l)).hasMatch())
    {
        const QString frames = m.captured(1);
        const QString rpu = m.captured(2);
        QStringList out;
        const QLocale locale;
        out << step(tr("Dolby Vision: %1 frames, %2 RPU metadata, base layer %3 MB, enhancement layer %4 MB")
                        .arg(frames, rpu, locale.toString(m.captured(4).toDouble(), 'f', 1),
                             locale.toString(m.captured(5).toDouble(), 'f', 1)));
        if (frames == rpu)
            out << QString::fromUtf8("   ✔ ") + tr("one RPU per frame: Dolby Vision metadata complete");
        return out;
    }
    if ((m = durationRe.match(l)).hasMatch())
    {
        m_muxedSeconds = timeToSeconds(m.captured(1));
        return {};
    }
    if ((m = fpsRe.match(l)).hasMatch())
    {
        const QString text = tr("   Frame rate detected: %1 fps").arg(QLocale().toString(m.captured(1).toDouble(), 'f', 3));
        if (m_shownOnce.contains(text))
            return {};
        m_shownOnce.insert(text);
        return {text};
    }
    // per-track decoding details use internal track numbers: the output stream table replaces them
    if (decodingRe.match(l).hasMatch() || l.startsWith("Drawing text subtitles into pictures"))
        return {};
    if ((m = framesRe.match(l)).hasMatch())
    {
        const QString text = tr("   %1 video frames processed").arg(m.captured(1));
        if (m_shownOnce.contains(text))
            return {};
        m_shownOnce.insert(text);
        return {text};
    }
    if (l == "Flushing write buffer")
        return {step(tr("Step: writing the last data"))};
    if (l.startsWith("Creating Blu-ray stream info"))
        return {step(tr("Step: creating the Blu-ray clip index (CLPI)"))};
    if (l.startsWith("Creating AVCHD stream info"))
        return {step(tr("Step: creating the AVCHD clip index (CLPI)"))};
    if (l.startsWith("Creating Blu-ray playlist") || l.startsWith("Creating AVCHD playlist"))
        return {step(tr("Step: creating the playlist (MPLS)"))};
    if (l == "Adding blank play list")
        return {step(tr("Step: adding the blank playlist"))};
    if (l == "Finalize ISO disk")
        return {step(tr("Step: finalizing the ISO image"))};
    if (l == "Mux successful complete" || l == "Demux complete.")
        return {step(QString::fromUtf8("✔ ") + tr("Completed successfully"))};
    static const QRegularExpression fontRe("^Font (.+) not found, using (.+) instead$");
    if ((m = fontRe.match(l)).hasMatch())
    {
        const QString text = tr("   Font %1 not installed: using %2 for the subtitles").arg(m.captured(1), m.captured(2));
        if (m_shownOnce.contains(text))
            return {};
        m_shownOnce.insert(text);
        return {text};
    }
    if (l.startsWith("Can't") || l.startsWith("Fatal"))
        return {QString::fromUtf8("✖ ") + l};
    if (l.startsWith("Warning"))
    {
        const QString w = warning(l);
        return w.isEmpty() ? QStringList() : QStringList{w};
    }
    if (l.startsWith("Error") || l.contains("error", Qt::CaseInsensitive))
        return {QString::fromUtf8("✖ ") + l};
    return {l};
}

QStringList EngineLog::summary(const int exitCode) const
{
    const double seconds = m_timer.isValid() ? m_timer.elapsed() / 1000.0 : 0;
    QStringList out;
    out << QString::fromUtf8("────── ") + tr("Summary") +
               QString::fromUtf8(" ──────");
    if (exitCode != 0)
    {
        out << QString::fromUtf8("✖ ") + tr("Failed with error code %1 after %2").arg(exitCode).arg(formatDuration(seconds));
        return out;
    }
    const QLocale locale;
    const qint64 size = outputSize(m_outputName);
    out << tr("Output: %1").arg(m_outputName);
    if (size > 0)
        out << tr("Size: %1").arg(locale.formattedDataSize(size, 2, QLocale::DataSizeTraditionalFormat));
    if (m_muxedSeconds > 0)
    {
        out << tr("Duration: %1").arg(formatDuration(m_muxedSeconds));
        if (size > 0)
            out << tr("Average bitrate: %1 Mb/s").arg(locale.toString(size * 8.0 / m_muxedSeconds / 1e6, 'f', 1));
    }
    QString time = tr("Processing time: %1").arg(formatDuration(seconds));
    if (m_muxedSeconds > 0 && seconds >= 1)
        time += tr(" (x%1 real time)").arg(locale.toString(m_muxedSeconds / seconds, 'f', 1));
    out << time;
    if (m_warningCount > 0)
        out << QString::fromUtf8("⚠ ") + tr("%n warning(s), see above", "", m_warningCount);
    return out;
}
