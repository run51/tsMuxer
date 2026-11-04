#include "muxQueue.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

#include "tsMuxerProcess.h"
#include "tsmuxerwindow.h"

namespace
{
// the engine expects the output name quoted, as the main window passes it
QString quoted(const QString &val) { return val.startsWith('"') ? val : '"' + val + '"'; }

enum Column
{
    COL_OUTPUT,
    COL_TYPE,
    COL_STATE,
    COL_PROGRESS,
    COL_COUNT
};

const int MAX_LOG_CHARS = 2 * 1024 * 1024;
const int MAX_SAVED_LOG_CHARS = 64 * 1024;  // the end of each job's log is kept with it

// Beside the ini for a portable copy, in the user's application data otherwise (in the Docker image that is under
// /config, which survives the container).
QString queueFileName()
{
    const QFileInfo ini(tsMuxerGuiIniName());
    const QString dir = ini.exists() ? ini.absolutePath()
                                     : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("queue.json"));
}

const char* const STATE_KEYS[] = {"pending", "running", "done", "failed", "cancelled"};
}  // namespace

MuxQueue::MuxQueue(QWidget *parent) : QDialog(parent, Qt::Window)
{
    m_table = new QTableWidget(0, COL_COUNT, this);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_OUTPUT, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TYPE, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATE, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(COL_PROGRESS, 160);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_table);
    splitter->addWidget(m_log);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    m_summary = new QLabel(this);
    m_startBtn = new QPushButton(this);
    m_stopBtn = new QPushButton(this);
    m_upBtn = new QPushButton(this);
    m_downBtn = new QPushButton(this);
    m_removeBtn = new QPushButton(this);
    m_retryBtn = new QPushButton(this);
    m_clearBtn = new QPushButton(this);
    m_closeBtn = new QPushButton(this);

    auto editButtons = new QHBoxLayout;
    editButtons->addWidget(m_upBtn);
    editButtons->addWidget(m_downBtn);
    editButtons->addWidget(m_removeBtn);
    editButtons->addWidget(m_retryBtn);
    editButtons->addWidget(m_clearBtn);
    editButtons->addStretch();

    auto runButtons = new QHBoxLayout;
    runButtons->addWidget(m_summary, 1);
    runButtons->addWidget(m_startBtn);
    runButtons->addWidget(m_stopBtn);
    runButtons->addWidget(m_closeBtn);

    auto layout = new QVBoxLayout(this);
    layout->addLayout(editButtons);
    layout->addWidget(splitter, 1);
    layout->addLayout(runButtons);

    connect(m_startBtn, &QPushButton::clicked, this, &MuxQueue::start);
    connect(m_stopBtn, &QPushButton::clicked, this, &MuxQueue::stop);
    connect(m_upBtn, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_downBtn, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(m_removeBtn, &QPushButton::clicked, this, &MuxQueue::removeSelected);
    connect(m_retryBtn, &QPushButton::clicked, this, &MuxQueue::retrySelected);
    connect(m_clearBtn, &QPushButton::clicked, this, &MuxQueue::clearFinished);
    connect(m_closeBtn, &QPushButton::clicked, this, &MuxQueue::close);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        refreshButtons();
        showSelectedLog();
    });

    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &MuxQueue::onStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this, &MuxQueue::onStderr);
    connect(&m_proc, &QProcess::finished, this, &MuxQueue::onFinished);
    connect(&m_proc, &QProcess::errorOccurred, this, &MuxQueue::onProcessError);

    resize(820, 480);
    restore();
    retranslate();
    refreshTable();
}

void MuxQueue::save() const
{
    QJsonArray jobs;
    for (const Job &job : m_jobs)
    {
        QJsonObject o;
        o["meta"] = job.metaText;
        o["output"] = job.outputName;
        o["type"] = job.outputType;
        o["state"] = STATE_KEYS[static_cast<int>(job.state)];
        o["exitCode"] = job.exitCode;
        o["log"] = job.log.right(MAX_SAVED_LOG_CHARS);
        jobs.append(o);
    }
    const QString name = queueFileName();
    QDir().mkpath(QFileInfo(name).absolutePath());
    QSaveFile file(name);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(QJsonObject{{"jobs", jobs}}).toJson());
        file.commit();
    }
}

void MuxQueue::restore()
{
    QFile file(queueFileName());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray jobs = QJsonDocument::fromJson(file.readAll()).object().value("jobs").toArray();
    for (const QJsonValue &v : jobs)
    {
        const QJsonObject o = v.toObject();
        Job job;
        job.metaText = o["meta"].toString();
        job.outputName = o["output"].toString();
        job.outputType = o["type"].toString();
        job.exitCode = o["exitCode"].toInt();
        job.log = o["log"].toString();
        const QString state = o["state"].toString();
        for (int k = 0; k < 5; ++k)
            if (state == QLatin1String(STATE_KEYS[k]))
                job.state = static_cast<JobState>(k);
        // A job that was running when the program stopped did not finish: it waits again, from the start.
        if (job.state == JobState::Running)
        {
            job.state = JobState::Pending;
            job.log += (job.log.isEmpty() ? "" : "\n") +
                       tr("Interrupted when tsMuxeR was closed: the job will start again from the beginning.");
        }
        if (job.state == JobState::Done)
            job.progress = 1000;
        if (job.metaText.isEmpty() || job.outputName.isEmpty())
            continue;
        m_jobs << job;
        if (job.state == JobState::Pending)
            ++m_restored;
    }
}

int MuxQueue::pendingCount() const
{
    int n = 0;
    for (const Job &job : m_jobs) n += job.state == JobState::Pending;
    return n;
}

MuxQueue::~MuxQueue()
{
    stop();
#ifdef _WIN32
    if (m_taskbar)
        static_cast<ITaskbarList3 *>(m_taskbar)->Release();
#endif
}

void MuxQueue::retranslate()
{
    setWindowTitle(tr("Mux queue"));
    m_table->setHorizontalHeaderLabels({tr("Output"), tr("Type"), tr("State"), tr("Progress")});
    m_startBtn->setText(tr("Start queue"));
    m_stopBtn->setText(tr("Stop"));
    m_upBtn->setText(tr("Move up"));
    m_downBtn->setText(tr("Move down"));
    m_removeBtn->setText(tr("Remove"));
    m_retryBtn->setText(tr("Retry"));
    m_clearBtn->setText(tr("Clear finished"));
    m_closeBtn->setText(tr("Close"));
    for (int row = 0; row < m_jobs.size(); ++row) refreshRow(row);
    refreshSummary();
}

void MuxQueue::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

QString MuxQueue::stateText(JobState state) const
{
    switch (state)
    {
    case JobState::Pending:
        return tr("Waiting");
    case JobState::Running:
        return tr("Running");
    case JobState::Done:
        return tr("Done");
    case JobState::Failed:
        return tr("Error");
    case JobState::Cancelled:
        return tr("Cancelled");
    }
    return QString();
}

void MuxQueue::addJob(const QString &metaText, const QString &outputName, const QString &outputType)
{
    Job job;
    job.metaText = metaText;
    job.outputName = outputName;
    job.outputType = outputType;
    m_jobs << job;
    refreshTable();
    m_table->selectRow(m_jobs.size() - 1);
    save();
}

bool MuxQueue::isRunning() const { return m_running; }

bool MuxQueue::containsPendingOutput(const QString &outputName) const
{
    const auto target = QFileInfo(outputName).absoluteFilePath();
    for (const auto &job : m_jobs)
    {
        if ((job.state == JobState::Pending || job.state == JobState::Running) &&
            QFileInfo(job.outputName).absoluteFilePath().compare(target, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void MuxQueue::start()
{
    if (m_running)
        return;
    m_running = true;
    m_stopRequested = false;
    m_succeeded = 0;
    m_failed = 0;
    startNextJob();
}

void MuxQueue::stop()
{
    if (!m_running)
        return;
    m_stopRequested = true;
    if (m_proc.state() != QProcess::NotRunning)
    {
        m_proc.kill();
        m_proc.waitForFinished();
    }
}

void MuxQueue::startNextJob()
{
    m_current = -1;
    if (!m_stopRequested)
    {
        for (int i = 0; i < m_jobs.size(); ++i)
        {
            if (m_jobs[i].state == JobState::Pending)
            {
                m_current = i;
                break;
            }
        }
    }
    if (m_current < 0)
    {
        m_running = false;
        refreshButtons();
        refreshSummary();
        setTaskbarProgress(0, 0, false);
        if (!m_stopRequested)
            emit queueFinished(m_succeeded, m_failed);
        return;
    }

    Job &job = m_jobs[m_current];
    job.state = JobState::Running;
    job.progress = 0;
    job.log.clear();
    m_stdoutTail.clear();

    m_metaFile = QDir::toNativeSeparators(QDir::tempPath()) + QDir::separator() +
                 QString("tsMuxeR_queue_%1_%2.meta").arg(QCoreApplication::applicationPid()).arg(m_current);
    QFile file(m_metaFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(job.metaText.toUtf8()) < 0)
    {
        appendLog(m_current, tr("Can't create temporary meta file \"%1\"").arg(m_metaFile));
        finishCurrentJob(JobState::Failed, -1);
        return;
    }
    file.close();

    QDir().mkpath(QFileInfo(job.outputName).absolutePath());
    refreshRow(m_current);
    refreshButtons();
    refreshSummary();
    m_table->selectRow(m_current);
    m_engineLog.start(job.outputName);
    save();
    m_proc.start(tsMuxerBinaryPath(), tsMuxerEngineArgs({m_metaFile, quoted(job.outputName)}));
}

void MuxQueue::onStdout()
{
    if (m_current < 0)
        return;
    // progress is printed as "12.3% complete" lines, possibly split across reads
    m_stdoutTail += m_proc.readAllStandardOutput();
    const int lastBreak = qMax(m_stdoutTail.lastIndexOf('\n'), m_stdoutTail.lastIndexOf('\r'));
    if (lastBreak < 0)
        return;
    const QString text = QString::fromUtf8(m_stdoutTail.left(lastBreak + 1));
    m_stdoutTail.remove(0, lastBreak + 1);

    static const QRegularExpression progressRe("([0-9]+(?:\\.[0-9]+)?)% complete");
    QString logText;
    for (const auto &line : text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts))
    {
        auto match = progressRe.match(line);
        if (match.hasMatch())
            m_jobs[m_current].progress = int(match.captured(1).toDouble() * 10 + 0.5);
        else
            for (const auto &shown : m_engineLog.format(line)) logText += shown + '\n';
    }
    if (!logText.isEmpty())
        appendLog(m_current, logText.chopped(1));
    refreshRow(m_current);
    refreshSummary();
}

void MuxQueue::onStderr()
{
    if (m_current < 0)
        return;
    QStringList shown;
    for (const auto &line : QString::fromUtf8(m_proc.readAllStandardError()).split(QRegularExpression("[\r\n]+")))
        shown << m_engineLog.format(line);
    if (!shown.isEmpty())
        appendLog(m_current, shown.join('\n'));
}

void MuxQueue::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_current < 0)
        return;
    onStdout();
    onStderr();
    if (m_stopRequested)
        finishCurrentJob(JobState::Cancelled, exitCode);
    else if (exitStatus == QProcess::NormalExit && exitCode == 0)
        finishCurrentJob(JobState::Done, exitCode);
    else
        finishCurrentJob(JobState::Failed, exitCode);
}

void MuxQueue::onProcessError(QProcess::ProcessError error)
{
    // a crash or kill is reported through finished() as well
    if (error == QProcess::FailedToStart && m_current >= 0)
    {
        appendLog(m_current, tr("tsMuxeR not found!"));
        finishCurrentJob(JobState::Failed, -1);
    }
}

void MuxQueue::finishCurrentJob(JobState state, int exitCode)
{
    Job &job = m_jobs[m_current];
    job.state = state;
    job.exitCode = exitCode;
    if (state == JobState::Done)
    {
        job.progress = 1000;
        ++m_succeeded;
    }
    else if (state == JobState::Failed)
        ++m_failed;
    if (state != JobState::Cancelled)
        appendLog(m_current, m_engineLog.summary(state == JobState::Done ? 0 : (exitCode != 0 ? exitCode : -1))
                                 .join('\n'));
    if (!m_metaFile.isEmpty())
    {
        QFile::remove(m_metaFile);
        m_metaFile.clear();
    }
    refreshRow(m_current);
    save();
    startNextJob();
}

void MuxQueue::appendLog(int row, const QString &text)
{
    Job &job = m_jobs[row];
    if (job.log.size() > MAX_LOG_CHARS)
        return;
    if (!job.log.isEmpty())
        job.log += '\n';
    job.log += text;
    if (selectedRow() == row)
        m_log->appendPlainText(text);
}

int MuxQueue::selectedRow() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void MuxQueue::showSelectedLog()
{
    const int row = selectedRow();
    m_log->setPlainText(row >= 0 ? m_jobs[row].log : QString());
    m_log->moveCursor(QTextCursor::End);
}

void MuxQueue::removeSelected()
{
    const int row = selectedRow();
    if (row < 0 || row == m_current)
        return;
    m_jobs.removeAt(row);
    if (m_current > row)
        --m_current;
    refreshTable();
    save();
    if (!m_jobs.isEmpty())
        m_table->selectRow(qMin(row, m_jobs.size() - 1));
}

void MuxQueue::moveSelected(int delta)
{
    const int row = selectedRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_jobs.size())
        return;
    m_jobs.swapItemsAt(row, target);
    if (m_current == row)
        m_current = target;
    else if (m_current == target)
        m_current = row;
    refreshTable();
    m_table->selectRow(target);
    save();
}

void MuxQueue::retrySelected()
{
    const int row = selectedRow();
    if (row < 0 || m_jobs[row].state == JobState::Running || m_jobs[row].state == JobState::Pending)
        return;
    m_jobs[row].state = JobState::Pending;
    m_jobs[row].progress = 0;
    m_jobs[row].log.clear();
    refreshRow(row);
    refreshButtons();
    refreshSummary();
    showSelectedLog();
    save();
}

void MuxQueue::clearFinished()
{
    for (int i = m_jobs.size() - 1; i >= 0; --i)
    {
        const auto state = m_jobs[i].state;
        if (state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled)
        {
            m_jobs.removeAt(i);
            if (m_current > i)
                --m_current;
        }
    }
    refreshTable();
    save();
}

void MuxQueue::refreshRow(int row)
{
    const Job &job = m_jobs[row];
    auto setText = [&](int col, const QString &text) {
        auto item = m_table->item(row, col);
        if (!item)
        {
            item = new QTableWidgetItem;
            m_table->setItem(row, col, item);
        }
        item->setText(text);
        item->setToolTip(text);
    };
    setText(COL_OUTPUT, QDir::toNativeSeparators(job.outputName));
    setText(COL_TYPE, job.outputType);
    setText(COL_STATE, stateText(job.state));

    auto bar = qobject_cast<QProgressBar *>(m_table->cellWidget(row, COL_PROGRESS));
    if (!bar)
    {
        bar = new QProgressBar;
        bar->setRange(0, 1000);
        bar->setTextVisible(true);
        m_table->setCellWidget(row, COL_PROGRESS, bar);
    }
    bar->setValue(job.progress);
    bar->setFormat(QString::number(job.progress / 10.0, 'f', 1) + '%');
}

void MuxQueue::refreshTable()
{
    m_table->setRowCount(m_jobs.size());
    for (int row = 0; row < m_jobs.size(); ++row) refreshRow(row);
    refreshButtons();
    refreshSummary();
    showSelectedLog();
}

void MuxQueue::refreshButtons()
{
    const int row = selectedRow();
    bool hasPending = false;
    bool hasFinished = false;
    for (const auto &job : m_jobs)
    {
        hasPending |= job.state == JobState::Pending;
        hasFinished |= job.state == JobState::Done || job.state == JobState::Failed || job.state == JobState::Cancelled;
    }
    const bool rowFinished = row >= 0 && m_jobs[row].state != JobState::Pending && m_jobs[row].state != JobState::Running;
    m_startBtn->setEnabled(!m_running && hasPending);
    m_stopBtn->setEnabled(m_running);
    m_upBtn->setEnabled(row > 0);
    m_downBtn->setEnabled(row >= 0 && row < m_jobs.size() - 1);
    m_removeBtn->setEnabled(row >= 0 && row != m_current);
    m_retryBtn->setEnabled(rowFinished);
    m_clearBtn->setEnabled(hasFinished);
}

void MuxQueue::refreshSummary()
{
    int total = 0;
    int finished = 0;
    int progressSum = 0;
    bool anyFailed = false;
    for (const auto &job : m_jobs)
    {
        if (job.state == JobState::Cancelled)
            continue;
        ++total;
        if (job.state == JobState::Done || job.state == JobState::Failed)
            ++finished;
        progressSum += job.state == JobState::Failed ? 1000 : job.progress;
        anyFailed |= job.state == JobState::Failed;
    }
    const int percent = total > 0 ? progressSum / (total * 10) : 0;
    if (m_running)
    {
        m_summary->setText(tr("Job %1 of %2 - total %3%").arg(qMin(finished + 1, total)).arg(total).arg(percent));
        setWindowTitle(tr("Mux queue") + QString(" - %1%").arg(percent));
        setTaskbarProgress(progressSum, total * 1000, anyFailed);
    }
    else
    {
        m_summary->setText(tr("%n job(s) in the queue", "", m_jobs.size()));
        setWindowTitle(tr("Mux queue"));
    }
}

void MuxQueue::setTaskbarProgress(int value, int maximum, bool error)
{
#ifdef _WIN32
    // progress on the taskbar button of the main window, visible while minimized
    if (!m_taskbar)
    {
        ITaskbarList3 *taskbar = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                                    reinterpret_cast<void **>(&taskbar))))
            return;
        taskbar->HrInit();
        m_taskbar = taskbar;
    }
    auto taskbar = static_cast<ITaskbarList3 *>(m_taskbar);
    auto hwnd = reinterpret_cast<HWND>(parentWidget() ? parentWidget()->window()->winId() : winId());
    if (maximum <= 0)
    {
        taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
        return;
    }
    taskbar->SetProgressState(hwnd, error ? TBPF_ERROR : TBPF_NORMAL);
    taskbar->SetProgressValue(hwnd, static_cast<ULONGLONG>(value), static_cast<ULONGLONG>(maximum));
#else
    Q_UNUSED(value);
    Q_UNUSED(maximum);
    Q_UNUSED(error);
#endif
}
