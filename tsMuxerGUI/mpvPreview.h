#ifndef MPV_PREVIEW_H
#define MPV_PREVIEW_H

#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Plays a source in mpv with one of its audio tracks, and keeps mpv's audio delay and the track's delay in tsMuxeR in
// step, so the delay can be set by ear and eye while the film plays. tsMuxeR applies a delay by adding it to the
// timestamps of the audio and does nothing else to it, so mpv with the same delay shows what the output will be.
//
// mpv is driven over its JSON IPC (--input-ipc-server), a named pipe on Windows.
class MpvPreview : public QObject
{
    Q_OBJECT

   public:
    struct Source
    {
        QString url;           // the file holding the video, or bd://mpls/<n> for a Blu-ray playlist
        QString blurayDevice;  // for bd://: the disc, a folder holding BDMV or an image
        QString audioFile;     // the file of the audio track when it is not the video's file
        qint64 audioSrcId = 0;  // the track's id in its file: Matroska track number, TS PID. 0: the file's first audio
        QString title;          // the window title
        QString hint;           // shown on the picture once playback starts
        double start = 0;       // where to start, in seconds
    };

    explicit MpvPreview(QObject* parent = nullptr);
    ~MpvPreview() override;

    // Starts mpv on the source with the given audio delay (mpv's own value, in ms). False when mpv could not start.
    bool start(const QString& mpv, const Source& source, int delayMs);
    void stop();
    [[nodiscard]] bool isRunning() const;
    // Where playback is, in seconds; asked of mpv, so 0 when it does not answer.
    double position();

    void setDelay(int delayMs);
    void setTitle(const QString& title);
    void showText(const QString& text, int ms = 2000);
    // Brings the window in front of the others.
    void raise();

   signals:
    // The delay was changed in mpv, by its keys (mpv's own value, in ms).
    void delayChanged(int delayMs);
    // mpv has no audio track with the source's id and plays another one.
    void trackNotFound();
    void finished();

   private:
    void tryConnect();
    void onConnected();
    void onReadyRead();
    void onMessage(const QJsonObject& msg);
    void selectAudio(const QJsonArray& tracks);
    void send(const QJsonArray& command, int requestId = 0);

    QProcess m_process;
    QLocalSocket m_socket;
    QTimer m_connectTimer;
    QString m_serverName;
    QByteArray m_buffer;
    Source m_source;
    int m_delayMs = 0;
    int m_connectTries = 0;
    bool m_audioSelected = false;
    // A reply waited for by position()
    int m_waitedRequest = 0;
    QJsonValue m_waitedReply;
    bool m_replyArrived = false;
};

#endif  // MPV_PREVIEW_H
