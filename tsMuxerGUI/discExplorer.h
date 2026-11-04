#ifndef DISC_EXPLORER_H_
#define DISC_EXPLORER_H_

#include <QDialog>
#include <QList>
#include <QStringList>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

// What a Blu-ray holds, as MKVToolNix shows it: every playlist with its duration, clips, size, chapters and angles,
// and for the one selected its streams and clips. Read by the engine (--disc-info) from a folder holding BDMV or
// from a disc image, without opening the clips. The longest playlist, the film on nearly every disc, is selected;
// short playlists and copies of a longer one (the decoys some discs add) are hidden until asked for.
class DiscExplorer : public QDialog
{
    Q_OBJECT
   public:
    struct Stream
    {
        int pid = 0;
        QChar kind;  // V, A, S or O
        QString codec;
        QString lang;
        QString details;
    };
    struct Clip
    {
        QString name;
        double seconds = 0;
        qint64 bytes = 0;
    };
    struct Playlist
    {
        QString name;
        double seconds = 0;
        int clipCount = 0;
        qint64 bytes = 0;
        int chapters = 0;
        int angles = 1;
        QStringList flags;
        QList<Clip> clips;
        QList<Stream> streams;
        int copies = 0;       // later playlists with the same clips and duration
        bool isCopy = false;  // the same clips and duration as a playlist listed before it
    };

    // root: a disc image, or the folder that holds BDMV
    DiscExplorer(const QString& root, QWidget* parent);
    // Reads the disc. false when there is nothing to show; the reason has been said.
    bool load();

   signals:
    void loadPlaylist(const QString& path);
    void queuePlaylists(const QStringList& paths);

   private:
    void fillPlaylists();
    void showDetails();
    void updateButtons();
    QList<int> selectedPlaylists() const;
    QString playlistPath(int index) const;

    QString m_root;
    QList<Playlist> m_playlists;
    bool m_encrypted = false;

    QLabel* m_header;
    QCheckBox* m_showAll;
    QTableWidget* m_table;
    QTableWidget* m_streams;
    QTableWidget* m_clips;
    QLabel* m_selectionInfo;
    QPushButton* m_loadBtn;
    QPushButton* m_queueBtn;
};

#endif
