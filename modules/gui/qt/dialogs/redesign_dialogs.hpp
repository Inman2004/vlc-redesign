/*****************************************************************************
 * redesign_dialogs.hpp : Figma dialogs of the redesigned interface (P4)
 *****************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_QT_REDESIGN_DIALOGS_HPP_
#define VLC_QT_REDESIGN_DIALOGS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QStringList>
#include <QList>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QComboBox;
class QListWidget;
class QFrame;
class QScrollArea;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/* Minimal api.opensubtitles.com (REST v1) client: search by movie hash,
 * title and language, then download through the API. The user supplies
 * their own API key (decision D17: no project key is bundled). */
class OpenSubtitlesClient : public QObject
{
    Q_OBJECT
public:
    struct Result
    {
        int fileId;
        QString fileName, language, release;
        int downloads;
        bool hashMatch;
    };
    explicit OpenSubtitlesClient( QObject *parent );
    static QString apiKey( intf_thread_t * );
    static void setApiKey( intf_thread_t *, const QString & );
    static QString movieHash( const QString &path );

    void search( const QString &key, const QString &hash, const QString &query,
                 const QString &language );
    void download( const QString &key, int fileId, const QString &destPath );

signals:
    void searchFinished( const QList<OpenSubtitlesClient::Result> & );
    void downloaded( const QString &path );
    void failed( const QString &message );

private:
    QNetworkReply *request( const QString &key, const QString &path,
                            const QByteArray &postBody = QByteArray() );
    static QString errorText( QNetworkReply * );
    QNetworkAccessManager *net;
    QNetworkReply *pendingSearch;
};

/* Frameless dialog of the Figma "row 3" frames: 50px #090909 title bar
 * with the title and a close button, a body, and a footer with Cancel on
 * the left and the primary action on the right (100x32 each). */
class RedesignDialog : public QDialog
{
    Q_OBJECT
public:
    RedesignDialog( intf_thread_t *, const QString &title, QWidget *parent );

protected:
    QWidget *header() const { return headerWidget; }
    QVBoxLayout *body() const { return bodyLayout; }
    QPushButton *primaryButton() const { return primary; }
    /* "Title (suffix)" + description, as in "Playlist Name (optional)" */
    void addSectionHeading( QVBoxLayout *, const QString &title,
                            const QString &description,
                            const QString &suffix = QString() );
    void addRule( QVBoxLayout * );
    void setDesignSize( int w, int h );
    bool eventFilter( QObject *, QEvent * ) override;
    void showEvent( QShowEvent * ) override;

    intf_thread_t *p_intf;

private:
    QFrame *frame;
    QWidget *headerWidget;
    QVBoxLayout *bodyLayout;
    QPushButton *primary;
    QSize designSize;
};

/* "Open Network Stream": several links, optionally kept as a playlist */
class OpenNetworkDialog : public RedesignDialog
{
    Q_OBJECT
public:
    OpenNetworkDialog( intf_thread_t *, QWidget *parent );

private slots:
    void validate();
    void accept() override;

private:
    QStringList urls() const;
    QPlainTextEdit *urlEdit;
    QLineEdit *nameEdit;
};

/* "New Playlist": title, thumbnail and files, saved as XSPF */
class NewPlaylistDialog : public RedesignDialog
{
    Q_OBJECT
public:
    NewPlaylistDialog( intf_thread_t *, QWidget *parent,
                       const QStringList &initialUris = QStringList() );

protected:
    bool eventFilter( QObject *, QEvent * ) override;

private slots:
    void validate();
    void pickThumbnail();
    void pickFiles();
    void accept() override;

private:
    void addFiles( const QStringList &uris );
    QLineEdit *nameEdit;
    QToolButton *thumbButton;
    QString thumbPath;
    QFrame *dropZone;
    QLabel *dropHint;
    QToolButton *dropAdd;
    QListWidget *fileList;
};

/* "Add Subtitles": scope (file / folder / playlist) x source (computer /
 * OpenSubtitles / link). Folder and playlist scopes pair every video with
 * a subtitle and remember the choice. */
class AddSubtitlesDialog : public RedesignDialog
{
    Q_OBJECT
public:
    enum { SCOPE_FILE, SCOPE_FOLDER, SCOPE_PLAYLIST };
    /* mediaUri: the file to add subtitles to (default: the playing one) */
    AddSubtitlesDialog( intf_thread_t *, QWidget *parent,
                        const QString &mediaUri = QString(), int scope = SCOPE_FILE );

protected:
    bool eventFilter( QObject *, QEvent * ) override;

private slots:
    void rebuild();
    void pickFolder();
    void accept() override;
    void searchOnline();
    void showResults( const QList<OpenSubtitlesClient::Result> & );
    void downloadNext();

private:
    struct Pair { QString video; QString subtitle; };
    void showMessage( const QString &text );
    void showKeyPrompt();
    QString defaultQuery() const;
    void buildFileList();
    void buildPairs( const QStringList &videos );
    void clearRows();
    QWidget *makeRow( const QString &icon, const QString &text, bool checkable,
                      bool checked, const QString &uri );

    QComboBox *scopeCombo, *sourceCombo, *languageCombo;
    QToolButton *pathButton;
    QLineEdit *linkEdit;
    QLineEdit *searchEdit;
    OpenSubtitlesClient *client;
    QTimer *searchTimer;
    QList<int> downloadQueue;    /* file ids still to fetch */
    QStringList downloadedUris;
    QVBoxLayout *rowsLayout;
    QScrollArea *rowsArea;
    QString folder;          /* folder of the current media */
    QString currentUri;      /* current media */
    QList<Pair> pairs;
    QList<QWidget *> rows;
};

/* Subtitle choices remembered per media (P4 "To This Folder / Playlist") */
namespace SubtitleScope
{
    void remember( intf_thread_t *, const QString &mediaUri, const QStringList &subs );
    QStringList lookup( intf_thread_t *, const QString &mediaUri );
    /* attach the remembered subtitles to a playlist item before it plays,
     * so VLC's own autodetection does not shadow them */
    void attachToItem( intf_thread_t *, int playlistItemId );
    /* attachToItem for everything already queued (startup) */
    void attachToQueued( intf_thread_t * );
    /* fallback for an item that started before attachToItem could run */
    void applyToCurrent( intf_thread_t * );
    /* playlist folder (VLC data dir/playlists), created on demand */
    QString playlistDir();
    bool writeXspf( const QString &path, const QString &title,
                    const QString &imageUri, const QStringList &uris );
    /* track locations of an XSPF (title/image are filled in when given) */
    QStringList readXspf( const QString &path, QString *title = NULL, QString *image = NULL );
    bool appendToXspf( const QString &path, const QStringList &uris );
    /* saved playlists, newest first */
    QStringList savedPlaylists();
    /* URI of the item that is playing, empty if none */
    QString playingUri( intf_thread_t * );
}

#endif
