#include "mpvPreview.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>

namespace
{
// mpv expands ${property} in a window title and in the text it shows; a literal $ is written $$.
QString mpvLiteral(QString text) { return text.replace(QLatin1Char('$'), QLatin1String("$$")); }

constexpr int OBSERVE_DELAY = 1;
constexpr int REQUEST_TRACKS = 1;
}  // namespace

MpvPreview::MpvPreview(QObject* parent) : QObject(parent)
{
    m_connectTimer.setInterval(100);
    connect(&m_connectTimer, &QTimer::timeout, this, &MpvPreview::tryConnect);
    connect(&m_socket, &QLocalSocket::connected, this, &MpvPreview::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &MpvPreview::onReadyRead);
    connect(&m_process, &QProcess::finished, this, [this] {
        m_connectTimer.stop();
        m_socket.abort();
        m_buffer.clear();
        emit finished();
    });
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.setStandardErrorFile(QProcess::nullDevice());
}

MpvPreview::~MpvPreview()
{
    // The preview closes with tsMuxeR: its delay would no longer go anywhere.
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}

bool MpvPreview::start(const QString& mpv, const Source& source, const int delayMs)
{
    stop();
    m_source = source;
    m_delayMs = delayMs;
    m_audioSelected = false;
    m_buffer.clear();

    static int previews = 0;
    m_serverName = QStringLiteral("tsmuxer-mpv-%1-%2").arg(QCoreApplication::applicationPid()).arg(++previews);
#ifdef Q_OS_WIN
    const QString ipcServer = QStringLiteral("\\\\.\\pipe\\") + m_serverName;
#else
    m_serverName = QDir::temp().filePath(m_serverName + QStringLiteral(".sock"));
    const QString ipcServer = m_serverName;
#endif

    QStringList args = {
        "--input-ipc-server=" + ipcServer,
        // Paused until the track is chosen, so the first seconds are not heard with another one.
        "--pause",
        "--force-window=immediate",
        "--keep-open=always",
        "--idle=no",
        "--sid=no",
        "--sub-auto=no",
        "--audio-file-auto=no",
        "--save-position-on-quit=no",
        "--resume-playback=no",
        "--autofit-larger=80%x80%",
        // A 4K HEVC film decoded in software can stutter, and a picture running late on its sound misleads.
        "--hwdec=auto-safe",
        "--audio-delay=" + QString::number(delayMs / 1000.0, 'f', 3),
        "--title=" + mpvLiteral(source.title),
    };
    if (source.start > 0)
        args << "--start=" + QString::number(source.start, 'f', 3);
    if (!source.blurayDevice.isEmpty())
        args << "--bluray-device=" + QDir::toNativeSeparators(source.blurayDevice);
    if (!source.audioFile.isEmpty())
        args << "--audio-file=" + QDir::toNativeSeparators(source.audioFile);
    args << "--" << (source.url.startsWith(QLatin1String("bd://")) ? source.url : QDir::toNativeSeparators(source.url));

    m_process.start(mpv, args);
    if (!m_process.waitForStarted(5000))
        return false;
    m_connectTries = 0;
    m_connectTimer.start();
    return true;
}

void MpvPreview::stop()
{
    m_connectTimer.stop();
    if (m_process.state() == QProcess::NotRunning)
        return;
    send({"quit"});
    if (!m_process.waitForFinished(2000))
    {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

bool MpvPreview::isRunning() const { return m_process.state() != QProcess::NotRunning; }

void MpvPreview::tryConnect()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    // mpv makes its pipe once it has started; 15 s covers a slow disc or network share
    if (m_process.state() != QProcess::Running || ++m_connectTries > 150)
    {
        m_connectTimer.stop();
        return;
    }
    m_socket.connectToServer(m_serverName);
}

void MpvPreview::onConnected()
{
    m_connectTimer.stop();
    send({"observe_property", OBSERVE_DELAY, "audio-delay"});
    // Finer steps than mpv's own (Ctrl+ and Ctrl-, 100 ms), which are kept. Without mpv's message: tsMuxeR shows its
    // own value, which is not mpv's when the source already places the track.
    static const struct
    {
        const char* key;
        int ms;
    } STEPS[] = {{"+", 10},         {"KP_ADD", 10},       {"-", -10},     {"KP_SUBTRACT", -10},
                 {"Ctrl++", 100}, {"Ctrl+KP_ADD", 100}, {"Ctrl+-", -100}, {"Ctrl+KP_SUBTRACT", -100}};
    for (const auto& step : STEPS)
        send({"keybind", step.key, QStringLiteral("no-osd add audio-delay %1").arg(step.ms / 1000.0)});
    send({"set_property", "audio-delay", m_delayMs / 1000.0});
    // Empty while the file is still opening; then the file-loaded event asks again.
    send({"get_property", "track-list"}, REQUEST_TRACKS);
}

void MpvPreview::onReadyRead()
{
    m_buffer += m_socket.readAll();
    for (qsizetype end = m_buffer.indexOf('\n'); end >= 0; end = m_buffer.indexOf('\n'))
    {
        const QJsonDocument doc = QJsonDocument::fromJson(m_buffer.left(end));
        m_buffer.remove(0, end + 1);
        if (doc.isObject())
            onMessage(doc.object());
    }
}

void MpvPreview::onMessage(const QJsonObject& msg)
{
    const QString event = msg.value(QLatin1String("event")).toString();
    if (event == QLatin1String("property-change"))
    {
        const QJsonValue data = msg.value(QLatin1String("data"));
        if (msg.value(QLatin1String("id")).toInt() != OBSERVE_DELAY || !data.isDouble())
            return;
        const int ms = qRound(data.toDouble() * 1000);
        if (ms != m_delayMs)
        {
            m_delayMs = ms;
            emit delayChanged(ms);
        }
        return;
    }
    if (event == QLatin1String("file-loaded"))
    {
        if (!m_audioSelected)
            send({"get_property", "track-list"}, REQUEST_TRACKS);
        return;
    }
    const int requestId = msg.value(QLatin1String("request_id")).toInt();
    if (requestId == 0)
        return;
    if (requestId == m_waitedRequest)
    {
        m_waitedReply = msg.value(QLatin1String("data"));
        m_replyArrived = true;
    }
    else if (requestId == REQUEST_TRACKS && !m_audioSelected)
    {
        const QJsonArray tracks = msg.value(QLatin1String("data")).toArray();
        if (!tracks.isEmpty())
            selectAudio(tracks);
    }
}

void MpvPreview::selectAudio(const QJsonArray& tracks)
{
    m_audioSelected = true;
    // An audio file added beside the video is an external track in mpv. A Blu-ray lists a TrueHD track a second time
    // as its AC-3 core, with the same PID, after the others: the first one is the track itself.
    const bool external = !m_source.audioFile.isEmpty();
    int chosen = -1;
    for (const QJsonValue& value : tracks)
    {
        const QJsonObject track = value.toObject();
        if (track.value(QLatin1String("type")).toString() != QLatin1String("audio") ||
            track.value(QLatin1String("external")).toBool() != external)
            continue;
        if (m_source.audioSrcId == 0 || track.value(QLatin1String("src-id")).toInteger() == m_source.audioSrcId)
        {
            chosen = track.value(QLatin1String("id")).toInt();
            break;
        }
    }
    if (chosen > 0)
        send({"set_property", "aid", chosen});
    else
        emit trackNotFound();
    send({"set_property", "pause", false});
    if (!m_source.hint.isEmpty())
        showText(m_source.hint, 6000);
}

double MpvPreview::position()
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return 0;
    static int requests = 100;
    m_waitedRequest = ++requests;
    m_replyArrived = false;
    send({"get_property", "time-pos"}, m_waitedRequest);
    QElapsedTimer timer;
    timer.start();
    while (!m_replyArrived && timer.elapsed() < 1000 && m_socket.state() == QLocalSocket::ConnectedState)
        if (m_socket.waitForReadyRead(100))
            onReadyRead();
    m_waitedRequest = 0;
    return m_replyArrived && m_waitedReply.isDouble() ? m_waitedReply.toDouble() : 0;
}

void MpvPreview::setDelay(const int delayMs)
{
    if (delayMs == m_delayMs)
        return;
    m_delayMs = delayMs;
    send({"set_property", "audio-delay", delayMs / 1000.0});
}

void MpvPreview::setTitle(const QString& title)
{
    m_source.title = title;
    send({"set_property", "title", mpvLiteral(title)});
}

void MpvPreview::showText(const QString& text, const int ms) { send({"show-text", mpvLiteral(text), ms}); }

void MpvPreview::raise()
{
    // A window cannot take the focus from another program on Windows; on top for a moment brings it in front.
    send({"set_property", "ontop", true});
    QTimer::singleShot(500, this, [this] { send({"set_property", "ontop", false}); });
}

void MpvPreview::send(const QJsonArray& command, const int requestId)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;
    QJsonObject msg{{QLatin1String("command"), command}};
    if (requestId)
        msg.insert(QLatin1String("request_id"), requestId);
    m_socket.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    m_socket.flush();
}
