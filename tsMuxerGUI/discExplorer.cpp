#include "discExplorer.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "tsMuxerProcess.h"

namespace
{
QString duration(const double seconds)
{
    const int s = static_cast<int>(seconds + 0.5);
    return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

QString sizeText(const qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    if (bytes >= 1000000000)
        return DiscExplorer::tr("%1 GB").arg(QLocale().toString(bytes / 1e9, 'f', 1));
    return DiscExplorer::tr("%1 MB").arg(QLocale().toString(bytes / 1e6, 'f', 0));
}

QTableWidgetItem* cell(const QString& text, const bool right = false)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (right)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QTableWidget* makeTable(QWidget* parent, const QStringList& headers)
{
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(t->fontMetrics().height() + 8);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
}  // namespace

DiscExplorer::DiscExplorer(const QString& root, QWidget* parent) : QDialog(parent, Qt::Window), m_root(root)
{
    setWindowTitle(tr("Disc explorer - %1").arg(QFileInfo(root).fileName()));

    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_showAll = new QCheckBox(tr("Also show short playlists and copies"), this);

    m_table = makeTable(this, {tr("Playlist"), tr("Duration"), tr("Clips"), tr("Size"), tr("Chapters"), tr("Notes")});
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_streams = makeTable(this, {tr("PID"), tr("Type"), tr("Codec"), tr("Language"), tr("Details")});
    m_streams->setSelectionMode(QAbstractItemView::NoSelection);
    m_clips = makeTable(this, {tr("Clip"), tr("Duration"), tr("Size")});
    m_clips->setSelectionMode(QAbstractItemView::NoSelection);
    auto* details = new QTabWidget(this);
    details->addTab(m_streams, tr("Tracks"));
    details->addTab(m_clips, tr("Clips"));

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_table);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    m_selectionInfo = new QLabel(this);
    m_loadBtn = new QPushButton(tr("Load this playlist"), this);
    m_loadBtn->setToolTip(tr("Replaces the input files with the selected playlist, its tracks shown as for any file."));
    m_queueBtn = new QPushButton(tr("Add selection to the queue"), this);
    m_queueBtn->setToolTip(
        tr("Each selected playlist becomes a job of the mux queue, with the current output type and options, named "
           "after the disc and the playlist. For the episodes of a series, for instance."));
    auto* closeBtn = new QPushButton(tr("Close"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_selectionInfo, 1);
    buttons->addWidget(m_loadBtn);
    buttons->addWidget(m_queueBtn);
    buttons->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_header);
    layout->addWidget(m_showAll);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(m_showAll, &QCheckBox::toggled, this, &DiscExplorer::fillPlaylists);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this]
            {
                showDetails();
                updateButtons();
            });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { m_loadBtn->click(); });
    connect(m_loadBtn, &QPushButton::clicked, this,
            [this]
            {
                const QList<int> sel = selectedPlaylists();
                if (sel.size() != 1)
                    return;
                const QString path = playlistPath(sel.first());
                accept();
                emit loadPlaylist(path);
            });
    connect(m_queueBtn, &QPushButton::clicked, this,
            [this]
            {
                QStringList paths;
                for (const int i : selectedPlaylists()) paths << playlistPath(i);
                if (paths.isEmpty())
                    return;
                accept();
                emit queuePlaylists(paths);
            });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    resize(900, 620);
}

bool DiscExplorer::load()
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    proc.start(tsMuxerBinaryPath(), tsMuxerEngineArgs({"--disc-info", QDir::toNativeSeparators(m_root)}));
    const bool finished = proc.waitForFinished(180000);
    QApplication::restoreOverrideCursor();

    QMap<QString, int> byName;
    for (const QString& raw : QString::fromUtf8(proc.readAll()).split('\n'))
    {
        const QString line = raw.trimmed();
        if (line == QLatin1String("Encrypted: 1"))
            m_encrypted = true;
        else if (line.startsWith(QLatin1String("Disc playlist: ")))
        {
            const QStringList f = line.mid(15).split('|');
            if (f.size() < 7)
                continue;
            Playlist pl;
            pl.name = f[0];
            pl.seconds = f[1].toDouble();
            pl.clipCount = f[2].toInt();
            pl.bytes = f[3].toLongLong();
            pl.chapters = f[4].toInt();
            pl.angles = f[5].toInt();
            pl.flags = f[6].split(',', Qt::SkipEmptyParts);
            byName[pl.name] = m_playlists.size();
            m_playlists << pl;
        }
        else if (line.startsWith(QLatin1String("Disc clip: ")))
        {
            const QStringList f = line.mid(11).split('|');
            if (f.size() >= 4 && byName.contains(f[0]))
                m_playlists[byName[f[0]]].clips << Clip{f[1], f[2].toDouble(), f[3].toLongLong()};
        }
        else if (line.startsWith(QLatin1String("Disc stream: ")))
        {
            const QStringList f = line.mid(13).split('|');
            if (f.size() >= 6 && byName.contains(f[0]))
                m_playlists[byName[f[0]]].streams << Stream{f[1].toInt(), f[2].isEmpty() ? QChar('O') : f[2][0],
                                                            f[3], f[4], f[5]};
        }
    }
    if (!finished || m_playlists.isEmpty())
    {
        QMessageBox::warning(parentWidget(), tr("Disc explorer"),
                             tr("No Blu-ray playlist was found in \"%1\".").arg(QDir::toNativeSeparators(m_root)));
        return false;
    }

    // A copy: the same clips in the same order and the same duration as a playlist listed before it. The engine
    // lists the longest first, so the one kept is the first of its kind.
    for (int i = 0; i < m_playlists.size(); ++i)
        for (int j = 0; j < i; ++j)
        {
            const Playlist& a = m_playlists[j];
            Playlist& b = m_playlists[i];
            if (a.isCopy || qAbs(a.seconds - b.seconds) > 0.5 || a.clips.size() != b.clips.size())
                continue;
            bool same = true;
            for (int k = 0; k < a.clips.size() && same; ++k) same = a.clips[k].name == b.clips[k].name;
            if (same)
            {
                b.isCopy = true;
                ++m_playlists[j].copies;
                break;
            }
        }

    m_header->setText(tr("<b>%1</b> - %n playlist(s). The longest one, selected, is usually the film.", "",
                         m_playlists.size())
                          .arg(QFileInfo(m_root).fileName().toHtmlEscaped()) +
                      (m_encrypted ? "<br><span style='color:#e05050'>" +
                                         tr("This disc is encrypted (AACS): tsMuxeR can only read a decrypted copy.") +
                                         "</span>"
                                   : QString()));
    fillPlaylists();
    return true;
}

void DiscExplorer::fillPlaylists()
{
    const double longest = m_playlists.isEmpty() ? 0 : m_playlists.first().seconds;
    const double shortLimit = qMin(300.0, longest / 2);
    const bool all = m_showAll->isChecked();
    m_table->setRowCount(0);
    for (int i = 0; i < m_playlists.size(); ++i)
    {
        const Playlist& pl = m_playlists[i];
        if (!all && i > 0 && (pl.isCopy || pl.seconds < shortLimit))
            continue;
        QStringList notes;
        if (pl.flags.contains("dv"))
            notes << tr("Dolby Vision");
        if (pl.flags.contains("3d"))
            notes << tr("3D");
        if (pl.angles > 1)
            notes << tr("%n angle(s)", "", pl.angles);
        if (pl.copies > 0)
            notes << tr("+%n identical copy(ies)", "", pl.copies);
        if (pl.isCopy)
            notes << tr("copy of a playlist above");
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* name = cell(pl.name);
        name->setData(Qt::UserRole, i);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, cell(duration(pl.seconds), true));
        m_table->setItem(row, 2, cell(QString::number(pl.clipCount), true));
        m_table->setItem(row, 3, cell(sizeText(pl.bytes), true));
        m_table->setItem(row, 4, cell(QString::number(pl.chapters), true));
        m_table->setItem(row, 5, cell(notes.join(", ")));
    }
    m_table->resizeColumnsToContents();
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
    const int hidden = m_playlists.size() - m_table->rowCount();
    m_showAll->setText(hidden > 0 ? tr("Also show short playlists and copies (%n hidden)", "", hidden)
                                  : tr("Also show short playlists and copies"));
    showDetails();
    updateButtons();
}

QList<int> DiscExplorer::selectedPlaylists() const
{
    QList<int> out;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows())
        out << m_table->item(index.row(), 0)->data(Qt::UserRole).toInt();
    std::sort(out.begin(), out.end());
    return out;
}

QString DiscExplorer::playlistPath(const int index) const
{
    return QDir::toNativeSeparators(m_root + "/BDMV/PLAYLIST/" + m_playlists[index].name);
}

void DiscExplorer::showDetails()
{
    m_streams->setRowCount(0);
    m_clips->setRowCount(0);
    const QList<int> sel = selectedPlaylists();
    if (sel.isEmpty())
        return;
    const Playlist& pl = m_playlists[sel.first()];
    int videos = 0;
    for (const Stream& s : pl.streams)
    {
        // the Dolby Vision table comes after the others: a second video of a "dv" playlist is its enhancement layer
        // the engine's words for the audio layout, in the window's language
        QString details = s.details;
        details.replace("stereo + multi-channel", tr("stereo + multi-channel"))
            .replace("multi-channel", tr("multi-channel"))
            .replace("stereo", tr("stereo"))
            .replace("mono", tr("mono"))
            .replace("secondary", tr("secondary"));
        if (s.kind == 'V' && ++videos > 1 && pl.flags.contains("dv"))
            details += (details.isEmpty() ? "" : ", ") + tr("Dolby Vision enhancement layer");
        const QString kind = s.kind == 'V'   ? tr("Video")
                             : s.kind == 'A' ? tr("Audio")
                             : s.kind == 'S' ? tr("Subtitles")
                                             : tr("Other");
        const int row = m_streams->rowCount();
        m_streams->insertRow(row);
        m_streams->setItem(row, 0, cell(QString("0x%1").arg(s.pid, 4, 16, QChar('0')).toUpper().replace("0X", "0x")));
        m_streams->setItem(row, 1, cell(kind));
        m_streams->setItem(row, 2, cell(s.codec));
        m_streams->setItem(row, 3, cell(s.lang));
        m_streams->setItem(row, 4, cell(details));
    }
    for (const Clip& c : pl.clips)
    {
        const int row = m_clips->rowCount();
        m_clips->insertRow(row);
        m_clips->setItem(row, 0, cell(c.name));
        m_clips->setItem(row, 1, cell(duration(c.seconds), true));
        m_clips->setItem(row, 2, cell(sizeText(c.bytes), true));
    }
    m_streams->resizeColumnsToContents();
    m_clips->resizeColumnsToContents();
}

void DiscExplorer::updateButtons()
{
    const QList<int> sel = selectedPlaylists();
    m_loadBtn->setEnabled(sel.size() == 1 && !m_encrypted);
    m_queueBtn->setEnabled(!sel.isEmpty() && !m_encrypted);
    double seconds = 0;
    qint64 bytes = 0;
    for (const int i : sel)
    {
        seconds += m_playlists[i].seconds;
        bytes += m_playlists[i].bytes;
    }
    m_selectionInfo->setText(sel.size() > 1 ? tr("%n playlist(s) selected: %1, %2", "", sel.size())
                                                  .arg(duration(seconds), sizeText(bytes))
                                            : QString());
}
