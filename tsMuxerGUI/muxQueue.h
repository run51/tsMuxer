#ifndef MUX_QUEUE_H_
#define MUX_QUEUE_H_

#include <QDialog>
#include <QList>
#include <QProcess>

#include "engineLog.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// Window holding a list of mux jobs that are run one after another. Each job
// is a snapshot of the main window settings (meta file text + output path)
// taken when it was queued, so the main window can be reused right away.
class MuxQueue : public QDialog
{
    Q_OBJECT
   public:
    enum class JobState
    {
        Pending,
        Running,
        Done,
        Failed,
        Cancelled,
    };

    struct Job
    {
        QString metaText;
        QString outputName;
        QString outputType;
        JobState state = JobState::Pending;
        int progress = 0;  // in tenths of a percent
        int exitCode = 0;
        QString log;
    };

    explicit MuxQueue(QWidget* parent);
    ~MuxQueue() override;

    void addJob(const QString& metaText, const QString& outputName, const QString& outputType);
    bool isRunning() const;
    bool containsPendingOutput(const QString& outputName) const;
    int pendingCount() const;
    // How many waiting jobs were read back from the previous session, 0 when none.
    int restoredCount() const { return m_restored; }
    void stop();

   signals:
    void queueFinished(int succeeded, int failed);

   protected:
    void changeEvent(QEvent* event) override;

   private:
    void retranslate();
    void start();
    void startNextJob();
    void onStdout();
    void onStderr();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void finishCurrentJob(JobState state, int exitCode);
    void appendLog(int row, const QString& text);
    void removeSelected();
    void moveSelected(int delta);
    void clearFinished();
    void retrySelected();
    void refreshRow(int row);
    void refreshTable();
    void refreshButtons();
    void refreshSummary();
    void showSelectedLog();
    int selectedRow() const;
    QString stateText(JobState state) const;
    void setTaskbarProgress(int value, int maximum, bool error);
    // The queue is kept in a file, so that a restart (of the PC, or of the NAS container) does not lose it.
    void save() const;
    void restore();
    int m_restored = 0;

    QList<Job> m_jobs;
    int m_current = -1;
    bool m_running = false;
    bool m_stopRequested = false;
    int m_succeeded = 0;
    int m_failed = 0;
    QString m_metaFile;
    QByteArray m_stdoutTail;
    QProcess m_proc;
    EngineLog m_engineLog;

    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    QLabel* m_summary;
    QPushButton* m_startBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_upBtn;
    QPushButton* m_downBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_retryBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_closeBtn;
    void* m_taskbar = nullptr;
};

#endif
