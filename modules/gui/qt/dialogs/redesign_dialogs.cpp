/*****************************************************************************
 * redesign_dialogs.cpp : Figma dialogs of the redesigned interface (P4)
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/redesign_dialogs.hpp"
#include "styles/vlc_theme.hpp"
#include "input_manager.hpp"
#include "recents.hpp"
#include "util/qt_dirs.hpp"

#include <vlc_input.h>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <vlc_url.h>
#include <vlc_input_item.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QStandardItemModel>
#include <QListWidget>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWindow>
#include <QScreen>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QSettings>
#include <QCryptographicHash>
#include <QXmlStreamWriter>
#include <QRegularExpression>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QAction>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QStyle>

enum { SHADOW = 24, TITLE_H = 50, BUTTON_W = 100, BUTTON_H = 32 };

/* "*.srt;*.ass;..." -> { "srt", "ass", ... } */
static QStringList suffixes( const char *patterns )
{
    QStringList out;
    foreach( QString p, QString::fromLatin1( patterns ).split( ';', QString::SkipEmptyParts ) )
        out << p.remove( "*." ).toLower();
    return out;
}

static QString toUri( const QString &path )
{
    return QUrl::fromLocalFile( path ).toString( QUrl::FullyEncoded );
}

static QString toPath( const QString &uri )
{
    return QUrl( uri ).toLocalFile();
}

/**********************************************************************
 * RedesignDialog
 **********************************************************************/
RedesignDialog::RedesignDialog( intf_thread_t *_p_intf, const QString &title, QWidget *parent )
    : QDialog( parent ), p_intf( _p_intf )
{
    setObjectName( "VLCRedesignDialog" );
    setWindowTitle( title );
    setWindowFlags( Qt::Dialog | Qt::FramelessWindowHint );
    setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_DeleteOnClose );
    setModal( true );

    QVBoxLayout *outer = new QVBoxLayout( this );
    outer->setContentsMargins( SHADOW, SHADOW, SHADOW, SHADOW );

    frame = new QFrame( this );
    frame->setObjectName( "RDFrame" );
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect( frame );
    shadow->setBlurRadius( 40 );
    shadow->setOffset( 0, 10 );
    shadow->setColor( QColor( 0, 0, 0, 150 ) );
    frame->setGraphicsEffect( shadow );
    outer->addWidget( frame );

    QVBoxLayout *fl = new QVBoxLayout( frame );
    fl->setContentsMargins( 0, 0, 0, 0 );
    fl->setSpacing( 0 );

    /* title bar: 50px #090909, title 14px at x 20, close glyph at the right */
    headerWidget = new QWidget( frame );
    headerWidget->setObjectName( "RDHeader" );
    headerWidget->setAttribute( Qt::WA_StyledBackground, true );
    QVBoxLayout *hv = new QVBoxLayout( headerWidget );
    hv->setContentsMargins( 0, 0, 0, 0 );
    hv->setSpacing( 0 );
    QWidget *titleRow = new QWidget( headerWidget );
    titleRow->setFixedHeight( TITLE_H );
    QHBoxLayout *th = new QHBoxLayout( titleRow );
    th->setContentsMargins( 20, 0, 10, 0 );
    QLabel *t = new QLabel( title, titleRow );
    t->setObjectName( "RDTitle" );
    th->addWidget( t );
    th->addStretch( 1 );
    QToolButton *close = new QToolButton( titleRow );
    close->setObjectName( "RDClose" );
    close->setToolTip( qtr( "Close (Esc)" ) );
    Theme::setupIconButton( close, "win-close", 32, 18 );
    connect( close, &QToolButton::clicked, this, &QDialog::reject );
    th->addWidget( close );
    hv->addWidget( titleRow );
    headerWidget->installEventFilter( this );
    titleRow->installEventFilter( this );
    fl->addWidget( headerWidget );

    QWidget *bodyWidget = new QWidget( frame );
    bodyWidget->setObjectName( "RDBody" );
    bodyLayout = new QVBoxLayout( bodyWidget );
    bodyLayout->setContentsMargins( 20, 28, 20, 0 );
    bodyLayout->setSpacing( 0 );
    fl->addWidget( bodyWidget, 1 );

    QWidget *footer = new QWidget( frame );
    footer->setObjectName( "RDFooter" );
    QHBoxLayout *ft = new QHBoxLayout( footer );
    ft->setContentsMargins( 20, 16, 20, 21 );
    QPushButton *cancel = new QPushButton( qtr( "Cancel" ), footer );
    cancel->setObjectName( "RDCancel" );
    cancel->setFixedSize( BUTTON_W, BUTTON_H );
    cancel->setCursor( Qt::PointingHandCursor );
    connect( cancel, &QPushButton::clicked, this, &QDialog::reject );
    primary = new QPushButton( qtr( "Save" ), footer );
    primary->setObjectName( "RDPrimary" );
    primary->setFixedSize( BUTTON_W, BUTTON_H );
    primary->setCursor( Qt::PointingHandCursor );
    primary->setDefault( true );
    connect( primary, &QPushButton::clicked, this, &QDialog::accept );
    ft->addWidget( cancel );
    ft->addStretch( 1 );
    ft->addWidget( primary );
    fl->addWidget( footer );
}

void RedesignDialog::addSectionHeading( QVBoxLayout *l, const QString &title,
                                        const QString &description, const QString &suffix )
{
    QLabel *t = new QLabel;
    t->setObjectName( "RDSection" );
    t->setTextFormat( Qt::RichText );
    t->setText( suffix.isEmpty() ? title.toHtmlEscaped()
              : QString( "%1 <span style='color:#6B6B6B'>%2</span>" )
                    .arg( title.toHtmlEscaped(), suffix.toHtmlEscaped() ) );
    l->addWidget( t );
    if( !description.isEmpty() )
    {
        l->addSpacing( 12 );
        QLabel *d = new QLabel( description );
        d->setObjectName( "RDDescription" );
        d->setWordWrap( true );
        l->addWidget( d );
    }
    l->addSpacing( 24 );
}

void RedesignDialog::addRule( QVBoxLayout *l )
{
    l->addSpacing( 21 );
    QFrame *r = new QFrame;
    r->setObjectName( "RDRule" );
    r->setFixedHeight( 1 );
    l->addWidget( r );
    l->addSpacing( 24 );
}

void RedesignDialog::setDesignSize( int w, int h )
{
    designSize = QSize( w, h );
}

void RedesignDialog::showEvent( QShowEvent *e )
{
    /* Figma size, but never larger than the main window (or screen) */
    QWidget *ref = parentWidget() ? parentWidget()->window() : NULL;
    QRect avail = ref ? ref->geometry()
                      : ( screen() ? screen()->availableGeometry() : QRect( 0, 0, 1920, 1080 ) );
    QSize s = designSize.isValid() ? designSize : sizeHint();
    s = s.boundedTo( avail.size() - QSize( 40, 40 ) ).expandedTo( minimumSizeHint() - QSize( 2 * SHADOW, 2 * SHADOW ) );
    resize( s + QSize( 2 * SHADOW, 2 * SHADOW ) );
    move( avail.center() - rect().center() );
    QDialog::showEvent( e );
}

bool RedesignDialog::eventFilter( QObject *o, QEvent *e )
{
    if( ( o == headerWidget || o->parent() == headerWidget )
     && e->type() == QEvent::MouseButtonPress
     && static_cast<QMouseEvent *>( e )->button() == Qt::LeftButton
     && windowHandle() )
    {
        windowHandle()->startSystemMove();
        return true;
    }
    return QDialog::eventFilter( o, e );
}

/**********************************************************************
 * Open Network Stream (1048 x 633 in the Figma frame)
 **********************************************************************/
OpenNetworkDialog::OpenNetworkDialog( intf_thread_t *_p_intf, QWidget *parent )
    : RedesignDialog( _p_intf, qtr( "Open Network Stream" ), parent )
{
    setDesignSize( 1048, 633 );
    QVBoxLayout *b = body();

    addSectionHeading( b, qtr( "Network URL" ), qtr( "You can enter multiple links to load." ) );
    urlEdit = new QPlainTextEdit;
    urlEdit->setObjectName( "RDTextArea" );
    urlEdit->setPlaceholderText( qtr( "e.g., https://www.example.com/stream.avi" ) );
    urlEdit->setFixedHeight( 192 );
    urlEdit->setTabChangesFocus( true );
    b->addWidget( urlEdit );

    addRule( b );
    addSectionHeading( b, qtr( "Playlist Name" ),
                       qtr( "The playlist will be saved and displayed with this title." ),
                       qtr( "(optional)" ) );
    nameEdit = new QLineEdit;
    nameEdit->setObjectName( "RDField" );
    nameEdit->setPlaceholderText( qtr( "e.g., Favorite TV Channels" ) );
    nameEdit->setFixedHeight( 34 );
    b->addWidget( nameEdit );
    b->addStretch( 1 );

    primaryButton()->setText( qtr( "Play" ) );
    primaryButton()->setToolTip( qtr( "Play the links; with a name they are also saved as a playlist" ) );
    connect( urlEdit, &QPlainTextEdit::textChanged, this, &OpenNetworkDialog::validate );
    connect( nameEdit, &QLineEdit::textChanged, this, &OpenNetworkDialog::validate );

    /* a link on the clipboard is the likely intent */
    validate();
    urlEdit->setFocus();
}

QStringList OpenNetworkDialog::urls() const
{
    QStringList out;
    foreach( const QString &tok, urlEdit->toPlainText().split( QRegularExpression( "\\s+" ),
                                                               QString::SkipEmptyParts ) )
    {
        QUrl u( tok );
        if( u.isValid() && !u.scheme().isEmpty() && ( !u.host().isEmpty() || u.scheme() == "file" ) )
            out << tok;
    }
    return out;
}

void OpenNetworkDialog::validate()
{
    const int n = urls().count();
    primaryButton()->setEnabled( n > 0 );
    primaryButton()->setText( nameEdit->text().trimmed().isEmpty() ? qtr( "Play" ) : qtr( "Save" ) );
}

void OpenNetworkDialog::accept()
{
    const QStringList list = urls();
    if( list.isEmpty() )
        return;
    const QString name = nameEdit->text().trimmed();
    if( !name.isEmpty() )
    {
        const QString path = SubtitleScope::playlistDir() + "/" + QString( name ).replace( '/', '-' ) + ".xspf";
        if( SubtitleScope::writeXspf( path, name, QString(), list ) )
        {
            Open::openMRL( p_intf, toUri( path ), true, true );
            QDialog::accept();
            return;
        }
        msg_Warn( p_intf, "could not write playlist %s", qtu( path ) );
    }
    for( int i = 0; i < list.count(); i++ )
        Open::openMRL( p_intf, list[i], i == 0, true );
    QDialog::accept();
}

/**********************************************************************
 * New Playlist (712 x 590 in the Figma frame)
 **********************************************************************/
NewPlaylistDialog::NewPlaylistDialog( intf_thread_t *_p_intf, QWidget *parent )
    : RedesignDialog( _p_intf, qtr( "New Playlist" ), parent )
{
    setDesignSize( 712, 590 );
    QVBoxLayout *b = body();

    /* Playlist Details: text column + 106px thumbnail square */
    QHBoxLayout *details = new QHBoxLayout;
    details->setSpacing( 24 );
    QVBoxLayout *col = new QVBoxLayout;
    col->setSpacing( 0 );
    addSectionHeading( col, qtr( "Playlist Details" ),
                       qtr( "The playlist will be saved and displayed with this title and thumbnail." ) );
    nameEdit = new QLineEdit;
    nameEdit->setObjectName( "RDField" );
    nameEdit->setPlaceholderText( qtr( "e.g., Favorite Videos" ) );
    nameEdit->setFixedHeight( 34 );
    col->addWidget( nameEdit );
    details->addLayout( col, 1 );
    thumbButton = new QToolButton;
    thumbButton->setObjectName( "RDThumb" );
    thumbButton->setFixedSize( 106, 106 );
    thumbButton->setIcon( Theme::icon( "add", 20 ) );
    thumbButton->setIconSize( QSize( 20, 20 ) );
    thumbButton->setCursor( Qt::PointingHandCursor );
    thumbButton->setToolTip( qtr( "Choose a thumbnail image" ) );
    connect( thumbButton, &QToolButton::clicked, this, &NewPlaylistDialog::pickThumbnail );
    details->addWidget( thumbButton, 0, Qt::AlignTop );
    b->addLayout( details );

    addRule( b );
    addSectionHeading( b, qtr( "Select Files" ), qtr( "These files will be added to the playlist." ) );

    /* dashed drop zone; shows the chosen files once there are some */
    dropZone = new QFrame;
    dropZone->setObjectName( "RDDropZone" );
    dropZone->setAcceptDrops( true );
    dropZone->setCursor( Qt::PointingHandCursor );
    dropZone->setMinimumHeight( 185 );
    dropZone->installEventFilter( this );
    QVBoxLayout *dz = new QVBoxLayout( dropZone );
    dz->setContentsMargins( 12, 12, 12, 12 );
    dz->setSpacing( 12 );
    dz->addStretch( 1 );
    dropHint = new QLabel( qtr( "Drag files into the box or click here to select files." ) );
    dropHint->setObjectName( "RDHint" );
    dropHint->setAlignment( Qt::AlignCenter );
    dz->addWidget( dropHint );
    fileList = new QListWidget;
    fileList->setObjectName( "RDFileList" );
    fileList->setSelectionMode( QAbstractItemView::ExtendedSelection );
    fileList->setToolTip( qtr( "Press Delete to remove the selected files" ) );
    fileList->installEventFilter( this );
    fileList->hide();
    dz->addWidget( fileList, 1 );
    dropAdd = new QToolButton;
    dropAdd->setObjectName( "RDAddButton" );
    dropAdd->setIcon( Theme::icon( "add", 16 ) );
    dropAdd->setIconSize( QSize( 16, 16 ) );
    dropAdd->setFixedSize( 84, 28 );
    dropAdd->setToolTip( qtr( "Add files" ) );
    connect( dropAdd, &QToolButton::clicked, this, &NewPlaylistDialog::pickFiles );
    dz->addWidget( dropAdd, 0, Qt::AlignHCenter );
    dz->addStretch( 1 );
    b->addWidget( dropZone, 1 );

    connect( nameEdit, &QLineEdit::textChanged, this, &NewPlaylistDialog::validate );
    validate();
    nameEdit->setFocus();
}

bool NewPlaylistDialog::eventFilter( QObject *o, QEvent *e )
{
    if( o == dropZone )
    {
        switch( e->type() )
        {
        case QEvent::DragEnter:
        {
            QDragEnterEvent *d = static_cast<QDragEnterEvent *>( e );
            if( d->mimeData()->hasUrls() )
            {
                d->acceptProposedAction();
                dropZone->setProperty( "dragging", true );
                dropZone->style()->unpolish( dropZone );
                dropZone->style()->polish( dropZone );
            }
            return true;
        }
        case QEvent::DragLeave:
        case QEvent::Drop:
            if( e->type() == QEvent::Drop )
            {
                QDropEvent *d = static_cast<QDropEvent *>( e );
                QStringList uris;
                foreach( const QUrl &u, d->mimeData()->urls() )
                    uris << u.toString( QUrl::FullyEncoded );
                addFiles( uris );
                d->acceptProposedAction();
            }
            dropZone->setProperty( "dragging", false );
            dropZone->style()->unpolish( dropZone );
            dropZone->style()->polish( dropZone );
            return true;
        case QEvent::MouseButtonRelease:
            if( fileList->isHidden() )
                pickFiles();
            return true;
        default:
            break;
        }
    }
    if( o == fileList && e->type() == QEvent::KeyPress )
    {
        QKeyEvent *k = static_cast<QKeyEvent *>( e );
        if( k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace )
        {
            qDeleteAll( fileList->selectedItems() );
            if( fileList->count() == 0 )
            {
                fileList->hide();
                dropHint->show();
            }
            validate();
            return true;
        }
    }
    return RedesignDialog::eventFilter( o, e );
}

void NewPlaylistDialog::addFiles( const QStringList &uris )
{
    foreach( const QString &uri, uris )
    {
        QUrl u( uri );
        QFileInfo fi( u.toLocalFile() );
        if( u.isLocalFile() && fi.isDir() )
        {
            /* a dropped folder adds its media, sorted */
            QDir d( fi.absoluteFilePath() );
            const QStringList media = suffixes( EXTENSIONS_MEDIA );
            foreach( const QFileInfo &f, d.entryInfoList( QDir::Files, QDir::Name ) )
                if( media.contains( f.suffix().toLower() ) )
                    addFiles( QStringList() << toUri( f.absoluteFilePath() ) );
            continue;
        }
        QListWidgetItem *it = new QListWidgetItem(
            u.isLocalFile() ? fi.fileName() : uri, fileList );
        it->setData( Qt::UserRole, uri );
        it->setToolTip( u.isLocalFile() ? toNativeSeparators( fi.absoluteFilePath() ) : uri );
        it->setIcon( Theme::icon( "video", 18 ) );
    }
    const bool any = fileList->count() > 0;
    fileList->setVisible( any );
    dropHint->setVisible( !any );
    validate();
}

void NewPlaylistDialog::pickFiles()
{
    QStringList files = QFileDialog::getOpenFileNames( this, qtr( "Select Files" ),
                            p_intf->p_sys->filepath.toLocalFile(),
                            QString( "%1 (%2);;%3 (*)" ).arg( qtr( "Media Files" ),
                                QString( EXTENSIONS_MEDIA ).replace( ';', ' ' ), qtr( "All Files" ) ) );
    QStringList uris;
    foreach( const QString &f, files )
        uris << toUri( f );
    if( !files.isEmpty() )
        p_intf->p_sys->filepath = QUrl::fromLocalFile( QFileInfo( files.first() ).absolutePath() );
    addFiles( uris );
}

void NewPlaylistDialog::pickThumbnail()
{
    QString f = QFileDialog::getOpenFileName( this, qtr( "Choose a thumbnail" ), QString(),
                    qtr( "Images" ) + " (*.png *.jpg *.jpeg *.webp *.bmp)" );
    if( f.isEmpty() )
        return;
    QImage img( f );
    if( img.isNull() )
        return;
    thumbPath = f;
    QPixmap px = QPixmap::fromImage( img.scaled( 106, 106, Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation ) );
    thumbButton->setIcon( QIcon( px ) );
    thumbButton->setIconSize( QSize( 102, 102 ) );
}

void NewPlaylistDialog::validate()
{
    primaryButton()->setEnabled( !nameEdit->text().trimmed().isEmpty() && fileList->count() > 0 );
}

void NewPlaylistDialog::accept()
{
    const QString name = nameEdit->text().trimmed();
    QStringList uris;
    for( int i = 0; i < fileList->count(); i++ )
        uris << fileList->item( i )->data( Qt::UserRole ).toString();
    if( name.isEmpty() || uris.isEmpty() )
        return;
    const QString base = SubtitleScope::playlistDir() + "/" + QString( name ).replace( '/', '-' );
    QString image;
    if( !thumbPath.isEmpty() )
    {
        QImage img( thumbPath );
        if( !img.isNull() && img.scaled( 256, 256, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation ).save( base + ".png" ) )
            image = toUri( base + ".png" );
    }
    if( !SubtitleScope::writeXspf( base + ".xspf", name, image, uris ) )
    {
        msg_Warn( p_intf, "could not write playlist %s.xspf", qtu( base ) );
        return;
    }
    /* add it to the queue without interrupting playback */
    Open::openMRL( p_intf, toUri( base + ".xspf" ), !THEMIM->getInput(), true );
    QDialog::accept();
}

/**********************************************************************
 * Add Subtitles (1048 x 798 in the Figma frame)
 **********************************************************************/
enum { SCOPE_FILE, SCOPE_FOLDER, SCOPE_PLAYLIST };
enum { SOURCE_COMPUTER, SOURCE_OPENSUBTITLES, SOURCE_LINK };

AddSubtitlesDialog::AddSubtitlesDialog( intf_thread_t *_p_intf, QWidget *parent )
    : RedesignDialog( _p_intf, qtr( "Add Subtitles" ), parent )
{
    setDesignSize( 1048, 798 );
    findChild<QWidget *>( "RDBody" )->setObjectName( "RDListBody" );
    /* list dialogs are one dark surface under the header (Figma) */
    findChild<QFrame *>( "RDFrame" )->setProperty( "list", true );
    body()->setContentsMargins( 0, 0, 0, 0 );

    input_thread_t *p_input = THEMIM->getInput();
    if( p_input )
    {
        char *uri = input_item_GetURI( input_GetItem( p_input ) );
        currentUri = qfu( uri );
        free( uri );
        if( QUrl( currentUri ).isLocalFile() )
            folder = QFileInfo( toPath( currentUri ) ).absolutePath();
    }

    /* toolbar under the title, inside the #090909 header */
    QWidget *bar = new QWidget( header() );
    QHBoxLayout *tb = new QHBoxLayout( bar );
    tb->setContentsMargins( 8, 0, 8, 8 );
    tb->setSpacing( 8 );
    scopeCombo = new QComboBox;
    scopeCombo->setObjectName( "RDCombo" );
    scopeCombo->addItem( qtr( "To This File" ) );
    scopeCombo->addItem( qtr( "To This Folder" ) );
    scopeCombo->addItem( qtr( "To This Playlist" ) );
    scopeCombo->setFixedSize( 196, 34 );
    sourceCombo = new QComboBox;
    sourceCombo->setObjectName( "RDCombo" );
    sourceCombo->addItem( qtr( "From Computer" ) );
    sourceCombo->addItem( qtr( "From OpenSubtitles" ) );
    sourceCombo->addItem( qtr( "From Link" ) );
    sourceCombo->setFixedSize( 196, 34 );
    sourceCombo->setItemData( SOURCE_OPENSUBTITLES,
        qtr( "Search opensubtitles.com (needs your free API key)" ), Qt::ToolTipRole );
    pathButton = new QToolButton;
    pathButton->setObjectName( "RDPath" );
    pathButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    pathButton->setIcon( Theme::icon( "folder", 18 ) );
    pathButton->setIconSize( QSize( 18, 18 ) );
    pathButton->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    pathButton->setFixedHeight( 34 );
    pathButton->setCursor( Qt::PointingHandCursor );
    pathButton->setToolTip( qtr( "Look for subtitles in another folder" ) );
    connect( pathButton, &QToolButton::clicked, this, &AddSubtitlesDialog::pickFolder );
    pathButton->installEventFilter( this ); /* re-elide the path on resize */
    linkEdit = new QLineEdit;
    linkEdit->setObjectName( "RDToolbarField" );
    linkEdit->setPlaceholderText( qtr( "https://example.com/subtitles.srt" ) );
    linkEdit->setFixedHeight( 34 );
    linkEdit->hide();
    searchEdit = new QLineEdit;
    searchEdit->setObjectName( "RDToolbarField" );
    searchEdit->setPlaceholderText( qtr( "Search by title" ) );
    searchEdit->setFixedHeight( 34 );
    searchEdit->addAction( Theme::icon( "search", 18 ), QLineEdit::LeadingPosition );
    searchEdit->setClearButtonEnabled( true );
    searchEdit->hide();
    languageCombo = new QComboBox;
    languageCombo->setObjectName( "RDCombo" );
    languageCombo->setFixedSize( 196, 34 );
    languageCombo->hide();
    {
        /* code, name; the first entry of sub-language is preselected */
        static const char *const langs[][2] = {
            { "en", "English" }, { "es", "Spanish" }, { "fr", "French" }, { "de", "German" },
            { "it", "Italian" }, { "pt-PT", "Portuguese" }, { "pt-BR", "Portuguese (Brazil)" },
            { "nl", "Dutch" }, { "pl", "Polish" }, { "ru", "Russian" }, { "tr", "Turkish" },
            { "ar", "Arabic" }, { "hi", "Hindi" }, { "ta", "Tamil" }, { "te", "Telugu" },
            { "ml", "Malayalam" }, { "ja", "Japanese" }, { "ko", "Korean" },
            { "zh-CN", "Chinese (simplified)" }, { "zh-TW", "Chinese (traditional)" },
            { "sv", "Swedish" }, { "no", "Norwegian" }, { "da", "Danish" }, { "fi", "Finnish" },
            { "el", "Greek" }, { "he", "Hebrew" }, { "id", "Indonesian" }, { "vi", "Vietnamese" },
        };
        languageCombo->addItem( qtr( "All Languages" ), QString() );
        for( auto &l : langs )
            languageCombo->addItem( qtr( l[1] ), QString( l[0] ) );
        char *psz = var_InheritString( p_intf, "sub-language" );
        const QString pref = qfu( psz ).split( ',' ).value( 0 ).trimmed().toLower();
        free( psz );
        int idx = languageCombo->findData( pref.isEmpty() ? QString( "en" ) : pref );
        languageCombo->setCurrentIndex( idx > 0 ? idx : 1 );
    }
    client = new OpenSubtitlesClient( this );
    searchTimer = new QTimer( this );
    searchTimer->setSingleShot( true );
    searchTimer->setInterval( 450 );
    connect( searchTimer, &QTimer::timeout, this, &AddSubtitlesDialog::searchOnline );
    connect( searchEdit, &QLineEdit::textEdited, searchTimer, QOverload<>::of( &QTimer::start ) );
    connect( searchEdit, &QLineEdit::returnPressed, this, &AddSubtitlesDialog::searchOnline );
    connect( languageCombo, QOverload<int>::of( &QComboBox::activated ), this, &AddSubtitlesDialog::searchOnline );
    connect( client, &OpenSubtitlesClient::searchFinished, this, &AddSubtitlesDialog::showResults );
    connect( client, &OpenSubtitlesClient::downloaded, this, [this]( const QString &path ) {
        downloadedUris << toUri( path );
        downloadNext();
    } );
    connect( client, &OpenSubtitlesClient::failed, this, [this]( const QString &msg ) {
        downloadQueue.clear();
        primaryButton()->setText( qtr( "Save" ) );
        primaryButton()->setEnabled( true );
        showMessage( msg );
    } );
    tb->addWidget( scopeCombo );
    tb->addWidget( sourceCombo );
    tb->addWidget( pathButton, 1 );
    tb->addWidget( linkEdit, 1 );
    tb->addWidget( searchEdit, 1 );
    tb->addWidget( languageCombo );
    static_cast<QVBoxLayout *>( header()->layout() )->addWidget( bar );

    rowsArea = new QScrollArea;
    rowsArea->setObjectName( "RDRows" );
    rowsArea->setWidgetResizable( true );
    rowsArea->setFrameShape( QFrame::NoFrame );
    rowsArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    QWidget *rowsHost = new QWidget;
    rowsHost->setObjectName( "RDRowsHost" );
    rowsLayout = new QVBoxLayout( rowsHost );
    rowsLayout->setContentsMargins( 8, 12, 8, 12 );
    rowsLayout->setSpacing( 4 );
    rowsLayout->addStretch( 1 );
    rowsArea->setWidget( rowsHost );
    body()->addWidget( rowsArea, 1 );

    connect( scopeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &AddSubtitlesDialog::rebuild );
    connect( sourceCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &AddSubtitlesDialog::rebuild );
    connect( linkEdit, &QLineEdit::textChanged, this, &AddSubtitlesDialog::rebuild );
    rebuild();
}

bool AddSubtitlesDialog::eventFilter( QObject *o, QEvent *e )
{
    if( o == pathButton && e->type() == QEvent::Resize )
        pathButton->setText( pathButton->fontMetrics().elidedText(
            pathButton->property( "fullText" ).toString(), Qt::ElideMiddle,
            qMax( 60, pathButton->width() - 56 ) ) );
    return RedesignDialog::eventFilter( o, e );
}

void AddSubtitlesDialog::pickFolder()
{
    QString d = QFileDialog::getExistingDirectory( this, qtr( "Subtitles folder" ), folder );
    if( d.isEmpty() )
        return;
    folder = d;
    rebuild();
}

void AddSubtitlesDialog::clearRows()
{
    qDeleteAll( rows );
    rows.clear();
    pairs.clear();
}

/* one list row: 36px, icon at 12, text 13px; checkable rows show an
 * accent check at the right and a #090909 pill when checked */
QWidget *AddSubtitlesDialog::makeRow( const QString &icon, const QString &text,
                                      bool checkable, bool checked, const QString &uri )
{
    QToolButton *r = new QToolButton;
    r->setObjectName( "RDRow" );
    r->setCheckable( checkable );
    r->setChecked( checked );
    r->setText( text );
    r->setToolTip( QUrl( uri ).isLocalFile() ? toNativeSeparators( toPath( uri ) ) : uri );
    r->setProperty( "uri", uri );
    r->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    r->setIcon( Theme::icon( icon, 18 ) );
    r->setIconSize( QSize( 18, 18 ) );
    r->setFixedHeight( 36 );
    r->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    if( checkable )
    {
        r->setCursor( Qt::PointingHandCursor );
        QLabel *check = new QLabel( r );
        check->setPixmap( Theme::pixmap( "check", 18, Theme::accent() ) );
        check->setAttribute( Qt::WA_TransparentForMouseEvents );
        check->setVisible( checked );
        QHBoxLayout *l = new QHBoxLayout( r );
        l->setContentsMargins( 0, 0, 12, 0 );
        l->addStretch( 1 );
        l->addWidget( check );
        connect( r, &QToolButton::toggled, check, &QLabel::setVisible );
        connect( r, &QToolButton::toggled, this, [this]() {
            bool any = false;
            foreach( QWidget *w, rows )
                if( QToolButton *b = qobject_cast<QToolButton *>( w ) )
                    any |= b->isChecked();
            primaryButton()->setEnabled( any );
        } );
    }
    return r;
}

/* 1px dashed connector (4 on, 4 off) between a video and its subtitle */
class DashLine : public QWidget
{
public:
    explicit DashLine( const QColor &c ) : color( c ) { setMinimumWidth( 24 ); }
protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter p( this );
        QPen pen( color, 1 );
        pen.setDashPattern( QVector<qreal>() << 4 << 4 );
        p.setPen( pen );
        const qreal y = height() / 2 + .5;
        p.drawLine( QPointF( 0, y ), QPointF( width(), y ) );
    }
private:
    QColor color;
};

static int episodeKey( const QString &name )
{
    /* S01E02, 1x02, or "Episode 2" */
    static const QRegularExpression se( "[Ss](\\d{1,2})[ ._-]?[Ee](\\d{1,3})" );
    static const QRegularExpression x( "(\\d{1,2})x(\\d{2,3})" );
    QRegularExpressionMatch m = se.match( name );
    if( !m.hasMatch() )
        m = x.match( name );
    if( m.hasMatch() )
        return m.captured( 1 ).toInt() * 1000 + m.captured( 2 ).toInt();
    return -1;
}

/* best subtitle for a video: same base name first, then same episode;
 * the preferred subtitle language (sub-language) breaks ties */
static QString bestSubtitle( intf_thread_t *p_intf, const QString &videoPath,
                             const QFileInfoList &subs )
{
    const QFileInfo v( videoPath );
    const QString base = v.completeBaseName().toLower();
    const int ep = episodeKey( v.fileName() );
    char *psz_lang = var_InheritString( p_intf, "sub-language" );
    const QStringList langs = qfu( psz_lang ).toLower().split( ',', QString::SkipEmptyParts );
    free( psz_lang );

    QString best;
    int bestScore = 0;
    foreach( const QFileInfo &s, subs )
    {
        const QString sb = s.completeBaseName().toLower();
        int score = 0;
        if( sb == base )
            score = 100;
        else if( sb.startsWith( base ) )
            score = 80;
        else if( ep >= 0 && episodeKey( s.fileName() ) == ep )
            score = 60;
        if( !score )
            continue;
        foreach( const QString &l, langs )
            if( sb.contains( l.trimmed() ) )
                score += 5;
        if( score > bestScore )
        {
            bestScore = score;
            best = toUri( s.absoluteFilePath() );
        }
    }
    return best;
}

void AddSubtitlesDialog::buildFileList()
{
    const QStringList subExt = suffixes( EXTENSIONS_SUBTITLE );
    QDir d( folder );
    const QString base = QFileInfo( toPath( currentUri ) ).completeBaseName().toLower();
    const int ep = episodeKey( QFileInfo( toPath( currentUri ) ).fileName() );
    const QStringList already = SubtitleScope::lookup( p_intf, currentUri );
    QFileInfoList all = d.entryInfoList( QDir::Files, QDir::Name );
    /* subtitles in a "Subs" folder next to the video count too */
    foreach( const QString &sub, QStringList() << "Subs" << "subs" << "Subtitles" )
        if( d.exists( sub ) )
            all << QDir( d.filePath( sub ) ).entryInfoList( QDir::Files, QDir::Name );
    QList<QFileInfo> related, others;
    foreach( const QFileInfo &f, all )
    {
        if( !subExt.contains( f.suffix().toLower() ) )
            continue;
        const QString b = f.completeBaseName().toLower();
        if( b.startsWith( base ) || ( ep >= 0 && episodeKey( f.fileName() ) == ep ) )
            related << f;
        else
            others << f;
    }
    /* the ones that belong to this file first */
    foreach( const QFileInfo &f, related + others )
    {
        const QString uri = toUri( f.absoluteFilePath() );
        QWidget *r = makeRow( "subtitles", f.fileName(), true, already.contains( uri ), uri );
        rowsLayout->insertWidget( rowsLayout->count() - 1, r );
        rows << r;
    }
    if( rows.isEmpty() )
    {
        QWidget *r = makeRow( "subtitles", qtr( "No subtitle files in this folder. Choose another folder above." ),
                              false, false, QString() );
        r->setEnabled( false );
        rowsLayout->insertWidget( rowsLayout->count() - 1, r );
        rows << r;
    }
}

/* "video ------ subtitle" pairs for a folder or the playlist */
void AddSubtitlesDialog::buildPairs( const QStringList &videos )
{
    const QStringList subExt = suffixes( EXTENSIONS_SUBTITLE );
    QHash<QString, QFileInfoList> subsByDir;
    foreach( const QString &uri, videos )
    {
        const QString path = toPath( uri );
        const QString dir = QFileInfo( path ).absolutePath();
        if( !subsByDir.contains( dir ) )
        {
            QFileInfoList subs;
            QDir d( scopeCombo->currentIndex() == SCOPE_FOLDER ? folder : dir );
            foreach( const QFileInfo &f, d.entryInfoList( QDir::Files, QDir::Name ) )
                if( subExt.contains( f.suffix().toLower() ) )
                    subs << f;
            subsByDir.insert( dir, subs );
        }
        Pair p;
        p.video = uri;
        p.subtitle = bestSubtitle( p_intf, path, subsByDir.value( dir ) );
        pairs << p;

        QWidget *row = new QWidget;
        row->setObjectName( "RDPairRow" );
        row->setFixedHeight( 36 );
        QHBoxLayout *l = new QHBoxLayout( row );
        l->setContentsMargins( 12, 0, 12, 0 );
        l->setSpacing( 10 );
        QLabel *vi = new QLabel;
        vi->setPixmap( Theme::pixmap( "video", 18, Theme::colors().textFaint ) );
        QLabel *vn = new QLabel( QFileInfo( path ).fileName() );
        vn->setObjectName( "RDRowText" );
        vn->setToolTip( toNativeSeparators( path ) );
        DashLine *dash = new DashLine( p.subtitle.isEmpty() ? QColor( 0x26, 0x26, 0x26 )
                                                            : QColor( 0x3A, 0x3A, 0x3A ) );
        QLabel *si = new QLabel;
        si->setPixmap( Theme::pixmap( "subtitles", 18, Theme::colors().textFaint ) );
        QLabel *sn = new QLabel( p.subtitle.isEmpty() ? qtr( "No match" )
                                                      : QFileInfo( toPath( p.subtitle ) ).fileName() );
        sn->setObjectName( p.subtitle.isEmpty() ? "RDRowFaint" : "RDRowText" );
        if( !p.subtitle.isEmpty() )
            sn->setToolTip( toNativeSeparators( toPath( p.subtitle ) ) );
        /* Figma: the subtitle column starts at the middle of the row, the
         * dashed connector fills the gap after the video name */
        QHBoxLayout *left = new QHBoxLayout;
        left->setSpacing( 10 );
        left->addWidget( vi );
        left->addWidget( vn );
        left->addWidget( dash, 1 );
        QHBoxLayout *right = new QHBoxLayout;
        right->setSpacing( 10 );
        right->addWidget( si );
        right->addWidget( sn, 1 );
        l->addLayout( left, 1 );
        l->addSpacing( 10 );
        l->addLayout( right, 1 );
        rowsLayout->insertWidget( rowsLayout->count() - 1, row );
        rows << row;
    }
    bool any = false;
    foreach( const Pair &p, pairs )
        any |= !p.subtitle.isEmpty();
    primaryButton()->setEnabled( any );
}

static void collectLeaves( playlist_item_t *node, QStringList &out )
{
    for( int i = 0; i < node->i_children; i++ )
    {
        playlist_item_t *c = node->pp_children[i];
        if( c->i_children >= 0 )
            collectLeaves( c, out );
        else
        {
            char *uri = input_item_GetURI( c->p_input );
            if( uri )
                out << qfu( uri );
            free( uri );
        }
    }
}

void AddSubtitlesDialog::rebuild()
{
    clearRows();
    const int scope = scopeCombo->currentIndex();
    const int source = sourceCombo->currentIndex();

    /* a link or an online search is for this file (Figma draws
     * "To This File / From OpenSubtitles") */
    const bool fileOnly = source == SOURCE_LINK || source == SOURCE_OPENSUBTITLES;
    if( fileOnly && scope != SCOPE_FILE )
    {
        QSignalBlocker b( scopeCombo );
        scopeCombo->setCurrentIndex( SCOPE_FILE );
    }
    scopeCombo->setEnabled( !fileOnly );
    pathButton->setVisible( source == SOURCE_COMPUTER );
    linkEdit->setVisible( source == SOURCE_LINK );
    searchEdit->setVisible( source == SOURCE_OPENSUBTITLES );
    languageCombo->setVisible( source == SOURCE_OPENSUBTITLES );
    primaryButton()->setText( qtr( "Save" ) );

    if( source == SOURCE_OPENSUBTITLES )
    {
        if( currentUri.isEmpty() )
        {
            showMessage( qtr( "Play a file first to search subtitles for it." ) );
            return;
        }
        if( OpenSubtitlesClient::apiKey( p_intf ).isEmpty() )
        {
            showKeyPrompt();
            return;
        }
        if( searchEdit->text().isEmpty() )
            searchEdit->setText( defaultQuery() );
        searchOnline();
        return;
    }
    {
        const QString full = folder.isEmpty() ? qtr( "Choose a folder" )
                                              : toNativeSeparators( folder ).replace( QDir::homePath(), "~" );
        pathButton->setProperty( "fullText", full );
        const int w = pathButton->width() > 100 ? pathButton->width() - 56 : 600;
        pathButton->setText( pathButton->fontMetrics().elidedText( full, Qt::ElideMiddle, w ) );
        pathButton->setToolTip( full );
    }

    if( source == SOURCE_LINK )
    {
        const QString link = linkEdit->text().trimmed();
        const QUrl u( link );
        const bool ok = u.isValid() && !u.scheme().isEmpty() && !u.host().isEmpty();
        if( ok )
        {
            QWidget *r = makeRow( "subtitles", link, true, true, link );
            rowsLayout->insertWidget( rowsLayout->count() - 1, r );
            rows << r;
        }
        primaryButton()->setEnabled( ok && !currentUri.isEmpty() );
        linkEdit->setFocus();
        return;
    }

    if( scope == SCOPE_FILE )
    {
        if( currentUri.isEmpty() )
        {
            QWidget *r = makeRow( "subtitles", qtr( "Play a file first to add subtitles to it." ),
                                  false, false, QString() );
            r->setEnabled( false );
            rowsLayout->insertWidget( rowsLayout->count() - 1, r );
            rows << r;
            primaryButton()->setEnabled( false );
            return;
        }
        buildFileList();
        bool any = false;
        foreach( QWidget *w, rows )
            if( QToolButton *b = qobject_cast<QToolButton *>( w ) )
                any |= b->isChecked();
        primaryButton()->setEnabled( any );
        return;
    }

    QStringList videos;
    const QStringList videoExt = suffixes( EXTENSIONS_VIDEO );
    if( scope == SCOPE_FOLDER )
    {
        QDir d( folder );
        foreach( const QFileInfo &f, d.entryInfoList( QDir::Files, QDir::Name ) )
            if( videoExt.contains( f.suffix().toLower() ) )
                videos << toUri( f.absoluteFilePath() );
    }
    else
    {
        playlist_Lock( THEPL );
        collectLeaves( THEPL->p_playing, videos );
        playlist_Unlock( THEPL );
        QStringList local;
        foreach( const QString &v, videos )
            if( QUrl( v ).isLocalFile() && videoExt.contains( QFileInfo( toPath( v ) ).suffix().toLower() ) )
                local << v;
        videos = local;
    }
    buildPairs( videos );
}

void AddSubtitlesDialog::accept()
{
    if( sourceCombo->currentIndex() == SOURCE_OPENSUBTITLES )
    {
        downloadQueue.clear();
        downloadedUris.clear();
        foreach( QWidget *w, rows )
            if( QToolButton *b = qobject_cast<QToolButton *>( w ) )
                if( b->isChecked() && b->property( "fileId" ).toInt() > 0 )
                    downloadQueue << b->property( "fileId" ).toInt();
        if( downloadQueue.isEmpty() )
            return;
        primaryButton()->setEnabled( false );
        primaryButton()->setText( qtr( "Downloading" ) );
        downloadNext();
        return;
    }
    input_thread_t *p_input = THEMIM->getInput();
    if( sourceCombo->currentIndex() == SOURCE_LINK || scopeCombo->currentIndex() == SCOPE_FILE )
    {
        QStringList chosen;
        foreach( QWidget *w, rows )
            if( QToolButton *b = qobject_cast<QToolButton *>( w ) )
                if( b->isChecked() )
                    chosen << b->property( "uri" ).toString();
        if( chosen.isEmpty() || currentUri.isEmpty() )
            return;
        SubtitleScope::remember( p_intf, currentUri, chosen );
        if( p_input )
            for( int i = 0; i < chosen.count(); i++ )
                input_AddSlave( p_input, SLAVE_TYPE_SPU, qtu( chosen[i] ), i == 0, true, false );
    }
    else
    {
        foreach( const Pair &p, pairs )
        {
            if( p.subtitle.isEmpty() )
                continue;
            SubtitleScope::remember( p_intf, p.video, QStringList() << p.subtitle );
            if( p_input && p.video == currentUri )
                input_AddSlave( p_input, SLAVE_TYPE_SPU, qtu( p.subtitle ), true, true, false );
        }
    }
    QDialog::accept();
}

/* OpenSubtitles results in the list, downloads one by one, then loads
 * them like local files (remembered for this file) */
QString AddSubtitlesDialog::defaultQuery() const
{
    /* "The_Wand_S01E01.mp4" -> "The Wand S01E01" */
    QString q = QFileInfo( toPath( currentUri ) ).completeBaseName();
    if( q.isEmpty() )
        q = QUrl( currentUri ).fileName();
    q.replace( QRegularExpression( "[._]+" ), " " );
    q.remove( QRegularExpression( "\\b(1080p|720p|2160p|480p|x26[45]|h26[45]|hevc|web-?dl|webrip|bluray|brrip|hdtv|aac|dts)\\b.*$",
                                  QRegularExpression::CaseInsensitiveOption ) );
    return q.simplified();
}

void AddSubtitlesDialog::showMessage( const QString &text )
{
    clearRows();
    QWidget *r = makeRow( "subtitles", text, false, false, QString() );
    r->setEnabled( false );
    rowsLayout->insertWidget( rowsLayout->count() - 1, r );
    rows << r;
    primaryButton()->setEnabled( false );
}

void AddSubtitlesDialog::showKeyPrompt()
{
    clearRows();
    QWidget *box = new QWidget;
    box->setObjectName( "RDKeyPrompt" );
    QVBoxLayout *l = new QVBoxLayout( box );
    l->setContentsMargins( 12, 12, 12, 12 );
    l->setSpacing( 12 );
    QLabel *t = new QLabel( qtr( "OpenSubtitles needs an API key" ) );
    t->setObjectName( "RDSection" );
    QLabel *d = new QLabel( qtr( "Create a free consumer key on opensubtitles.com, then paste it here. "
                                 "It is stored only in this computer's VLC settings." ) );
    d->setObjectName( "RDDescription" );
    d->setWordWrap( true );
    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing( 8 );
    QLineEdit *key = new QLineEdit;
    key->setObjectName( "RDField" );
    key->setFixedHeight( 34 );
    key->setPlaceholderText( qtr( "API key" ) );
    key->setEchoMode( QLineEdit::PasswordEchoOnEdit );
    QPushButton *use = new QPushButton( qtr( "Use Key" ) );
    use->setObjectName( "RDCancel" );
    use->setFixedSize( 100, 34 );
    QPushButton *get = new QPushButton( qtr( "Get a Key" ) );
    get->setObjectName( "RDCancel" );
    get->setFixedSize( 100, 34 );
    row->addWidget( key, 1 );
    row->addWidget( use );
    row->addWidget( get );
    l->addWidget( t );
    l->addWidget( d );
    l->addLayout( row );
    connect( get, &QPushButton::clicked, []() {
        QDesktopServices::openUrl( QUrl( "https://www.opensubtitles.com/en/consumers" ) );
    } );
    auto save = [this, key]() {
        if( key->text().trimmed().isEmpty() )
            return;
        OpenSubtitlesClient::setApiKey( p_intf, key->text().trimmed() );
        rebuild();
    };
    connect( use, &QPushButton::clicked, this, save );
    connect( key, &QLineEdit::returnPressed, this, save );
    rowsLayout->insertWidget( rowsLayout->count() - 1, box );
    rows << box;
    primaryButton()->setEnabled( false );
    key->setFocus();
}

void AddSubtitlesDialog::searchOnline()
{
    searchTimer->stop();
    if( sourceCombo->currentIndex() != SOURCE_OPENSUBTITLES )
        return;
    const QString key = OpenSubtitlesClient::apiKey( p_intf );
    if( key.isEmpty() )
    {
        showKeyPrompt();
        return;
    }
    showMessage( qtr( "Searching opensubtitles.com..." ) );
    const QString path = QUrl( currentUri ).isLocalFile() ? toPath( currentUri ) : QString();
    client->search( key, path.isEmpty() ? QString() : OpenSubtitlesClient::movieHash( path ),
                    searchEdit->text().trimmed(), languageCombo->currentData().toString() );
}

void AddSubtitlesDialog::showResults( const QList<OpenSubtitlesClient::Result> &results )
{
    if( sourceCombo->currentIndex() != SOURCE_OPENSUBTITLES )
        return;
    clearRows();
    if( results.isEmpty() )
    {
        showMessage( qtr( "No subtitles found. Try another title or language." ) );
        return;
    }
    foreach( const OpenSubtitlesClient::Result &r, results )
    {
        QString text = r.fileName.isEmpty() ? r.release : r.fileName;
        if( !r.language.isEmpty() )
            text += QString( "  ·  %1" ).arg( r.language.toUpper() );
        if( r.hashMatch )
            text += QString( "  ·  %1" ).arg( qtr( "matches this file" ) );
        QWidget *w = makeRow( "subtitles", text, true, false, QString() );
        w->setProperty( "fileId", r.fileId );
        w->setToolTip( qtr( "%1\nDownloads: %2" ).arg( r.release ).arg( r.downloads ) );
        rowsLayout->insertWidget( rowsLayout->count() - 1, w );
        rows << w;
    }
    primaryButton()->setEnabled( false );
}

void AddSubtitlesDialog::downloadNext()
{
    if( downloadQueue.isEmpty() )
    {
        if( downloadedUris.isEmpty() )
            return;
        SubtitleScope::remember( p_intf, currentUri, downloadedUris );
        if( input_thread_t *p_input = THEMIM->getInput() )
            for( int i = 0; i < downloadedUris.count(); i++ )
                input_AddSlave( p_input, SLAVE_TYPE_SPU, qtu( downloadedUris[i] ), i == 0, true, false );
        QDialog::accept();
        return;
    }
    const int id = downloadQueue.takeFirst();
    /* kept with VLC's data, named after the video */
    char *psz = config_GetUserDir( VLC_DATA_DIR );
    const QString dir = qfu( psz ) + "/subtitles";
    free( psz );
    QDir().mkpath( dir );
    const QString base = QFileInfo( toPath( currentUri ) ).completeBaseName();
    const QString dest = QString( "%1/%2.%3.%4.srt" ).arg( dir, base.isEmpty() ? QString( "subtitle" ) : base,
                                                          languageCombo->currentData().toString().isEmpty()
                                                              ? QString( "sub" ) : languageCombo->currentData().toString() )
                                                     .arg( id );
    client->download( OpenSubtitlesClient::apiKey( p_intf ), id, dest );
}

/**********************************************************************
 * OpenSubtitlesClient
 **********************************************************************/
static const char *const OS_API = "https://api.opensubtitles.com/api/v1";

OpenSubtitlesClient::OpenSubtitlesClient( QObject *parent )
    : QObject( parent ), pendingSearch( NULL )
{
    net = new QNetworkAccessManager( this );
}

QString OpenSubtitlesClient::apiKey( intf_thread_t *p_intf )
{
    return getSettings()->value( "OpenSubtitles/apiKey" ).toString();
}

void OpenSubtitlesClient::setApiKey( intf_thread_t *p_intf, const QString &key )
{
    getSettings()->setValue( "OpenSubtitles/apiKey", key );
}

/* OpenSubtitles movie hash: file size + the 64-bit little-endian word sums
 * of the first and last 64 KiB */
QString OpenSubtitlesClient::movieHash( const QString &path )
{
    QFile f( path );
    const qint64 size = f.size();
    const qint64 chunk = 64 * 1024;
    if( size < chunk || !f.open( QIODevice::ReadOnly ) )
        return QString();
    quint64 hash = size;
    auto sum = [&f, &hash, chunk]( qint64 at ) {
        f.seek( at );
        const QByteArray b = f.read( chunk );
        for( int i = 0; i + 8 <= b.size(); i += 8 )
            hash += qFromLittleEndian<quint64>( reinterpret_cast<const uchar *>( b.constData() + i ) );
    };
    sum( 0 );
    sum( qMax<qint64>( 0, size - chunk ) );
    return QString( "%1" ).arg( hash, 16, 16, QChar( '0' ) );
}

QNetworkReply *OpenSubtitlesClient::request( const QString &key, const QString &path,
                                             const QByteArray &postBody )
{
    QNetworkRequest req( QUrl( QString( OS_API ) + path ) );
    req.setRawHeader( "Api-Key", key.toUtf8() );
    req.setRawHeader( "User-Agent", "VLC v" PACKAGE_VERSION );
    req.setRawHeader( "Accept", "application/json" );
    req.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );
    if( postBody.isNull() )
        return net->get( req );
    req.setHeader( QNetworkRequest::ContentTypeHeader, "application/json" );
    return net->post( req, postBody );
}

QString OpenSubtitlesClient::errorText( QNetworkReply *r )
{
    const int status = r->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    const QJsonObject o = QJsonDocument::fromJson( r->readAll() ).object();
    const QString msg = o.value( "message" ).toString( o.value( "errors" ).toArray().first().toString() );
    if( status == 401 || status == 403 )
        return qtr( "OpenSubtitles refused the API key%1." ).arg( msg.isEmpty() ? QString() : ": " + msg );
    if( status == 406 || status == 429 )
        return qtr( "OpenSubtitles download limit reached%1." ).arg( msg.isEmpty() ? QString() : ": " + msg );
    if( !msg.isEmpty() )
        return qtr( "OpenSubtitles: %1" ).arg( msg );
    return qtr( "Could not reach opensubtitles.com (%1)." ).arg( r->errorString() );
}

void OpenSubtitlesClient::search( const QString &key, const QString &hash,
                                  const QString &query, const QString &language )
{
    if( pendingSearch )
    {
        pendingSearch->disconnect( this );
        pendingSearch->abort();
        pendingSearch->deleteLater();
    }
    QUrlQuery q;
    /* the API wants its parameters sorted */
    if( !language.isEmpty() )
        q.addQueryItem( "languages", language.toLower() );
    if( !hash.isEmpty() )
        q.addQueryItem( "moviehash", hash );
    if( !query.isEmpty() )
        q.addQueryItem( "query", query.toLower() );
    QNetworkReply *r = request( key, "/subtitles?" + q.toString( QUrl::FullyEncoded ) );
    pendingSearch = r;
    connect( r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        if( pendingSearch == r )
            pendingSearch = NULL;
        if( r->error() != QNetworkReply::NoError )
        {
            emit failed( errorText( r ) );
            return;
        }
        QList<Result> out;
        foreach( const QJsonValue &v, QJsonDocument::fromJson( r->readAll() ).object().value( "data" ).toArray() )
        {
            const QJsonObject a = v.toObject().value( "attributes" ).toObject();
            const QJsonArray files = a.value( "files" ).toArray();
            if( files.isEmpty() )
                continue;
            Result res;
            res.fileId = files.first().toObject().value( "file_id" ).toInt();
            res.fileName = files.first().toObject().value( "file_name" ).toString();
            res.language = a.value( "language" ).toString();
            res.release = a.value( "release" ).toString();
            res.downloads = a.value( "download_count" ).toInt();
            res.hashMatch = a.value( "moviehash_match" ).toBool();
            if( res.fileId > 0 )
                out << res;
        }
        /* exact file matches first, then the most downloaded */
        std::stable_sort( out.begin(), out.end(), []( const Result &x, const Result &y ) {
            return x.hashMatch != y.hashMatch ? x.hashMatch : x.downloads > y.downloads;
        } );
        emit searchFinished( out );
    } );
}

void OpenSubtitlesClient::download( const QString &key, int fileId, const QString &destPath )
{
    QJsonObject body;
    body.insert( "file_id", fileId );
    QNetworkReply *r = request( key, "/download", QJsonDocument( body ).toJson( QJsonDocument::Compact ) );
    connect( r, &QNetworkReply::finished, this, [this, r, destPath]() {
        r->deleteLater();
        if( r->error() != QNetworkReply::NoError )
        {
            emit failed( errorText( r ) );
            return;
        }
        const QString link = QJsonDocument::fromJson( r->readAll() ).object().value( "link" ).toString();
        if( link.isEmpty() )
        {
            emit failed( qtr( "OpenSubtitles did not return a download link." ) );
            return;
        }
        /* the link is a temporary, pre-signed file URL */
        QNetworkRequest fileReq( ( QUrl( link ) ) );
        fileReq.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );
        QNetworkReply *f = net->get( fileReq );
        connect( f, &QNetworkReply::finished, this, [this, f, destPath]() {
            f->deleteLater();
            QFile out( destPath );
            if( f->error() != QNetworkReply::NoError || !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
            {
                emit failed( f->error() != QNetworkReply::NoError ? errorText( f )
                                                                  : qtr( "Could not save the subtitle file." ) );
                return;
            }
            out.write( f->readAll() );
            out.close();
            emit downloaded( destPath );
        } );
    } );
}

/**********************************************************************
 * SubtitleScope
 **********************************************************************/
static QString scopeKey( const QString &uri )
{
    return "SubtitleScope/" + QString::fromLatin1(
        QCryptographicHash::hash( uri.toUtf8(), QCryptographicHash::Sha1 ).toHex() );
}

void SubtitleScope::remember( intf_thread_t *p_intf, const QString &mediaUri, const QStringList &subs )
{
    getSettings()->setValue( scopeKey( mediaUri ), subs );
}

QStringList SubtitleScope::lookup( intf_thread_t *p_intf, const QString &mediaUri )
{
    return getSettings()->value( scopeKey( mediaUri ) ).toStringList();
}

/* forcedOnly: VLC's autodetection also lists its subtitles on the item,
 * unforced; only a forced entry is guaranteed to be the selected one */
static bool itemHasSlave( input_item_t *item, const QString &uri, bool forcedOnly = false )
{
    bool found = false;
    vlc_mutex_lock( &item->lock );
    for( int i = 0; i < item->i_slaves; i++ )
        if( item->pp_slaves[i] && uri == qfu( item->pp_slaves[i]->psz_uri )
         && ( !forcedOnly || item->pp_slaves[i]->b_forced ) )
            found = true;
    vlc_mutex_unlock( &item->lock );
    return found;
}

static QStringList existingSubs( intf_thread_t *p_intf, input_item_t *item )
{
    char *psz_uri = input_item_GetURI( item );
    const QStringList subs = SubtitleScope::lookup( p_intf, qfu( psz_uri ) );
    free( psz_uri );
    QStringList out;
    foreach( const QString &s, subs )
        if( !QUrl( s ).isLocalFile() || QFileInfo::exists( toPath( s ) ) )
            out << s;
    return out;
}

/* media whose remembered subtitles are known to be loaded and selected in
 * this session: attached before they started, or added while playing */
static QSet<QString> handled;

void SubtitleScope::attachToItem( intf_thread_t *p_intf, int id )
{
    playlist_Lock( THEPL );
    playlist_item_t *pl = playlist_ItemGetById( THEPL, id );
    /* an item that is already playing has read its slaves (the append event
     * can arrive late); applyToCurrent() handles that one */
    if( pl && pl == playlist_CurrentPlayingItem( THEPL ) )
        pl = NULL;
    input_item_t *item = pl ? pl->p_input : NULL;
    if( item )
        input_item_Hold( item );
    playlist_Unlock( THEPL );
    if( !item )
        return;
    bool first = true;
    const QStringList subs = existingSubs( p_intf, item );
    foreach( const QString &s, subs )
    {
        if( !itemHasSlave( item, s ) )
        {
            input_item_slave_t *slave = input_item_slave_New( qtu( s ), SLAVE_TYPE_SPU,
                                                              SLAVE_PRIORITY_USER );
            if( slave )
            {
                slave->b_forced = first; /* the first choice is selected */
                if( input_item_AddSlave( item, slave ) != VLC_SUCCESS )
                    input_item_slave_Delete( slave );
            }
        }
        first = false;
    }
    if( !subs.isEmpty() )
    {
        char *psz_uri = input_item_GetURI( item );
        handled.insert( qfu( psz_uri ) );
        free( psz_uri );
    }
    input_item_Release( item );
}

void SubtitleScope::attachToQueued( intf_thread_t *p_intf )
{
    QList<int> ids;
    playlist_Lock( THEPL );
    /* the playing item already read its slaves; applyToCurrent handles it */
    playlist_item_t *playing = playlist_CurrentPlayingItem( THEPL );
    for( int i = 0; i < THEPL->items.i_size; i++ )
        if( THEPL->items.p_elems[i]->i_children < 0 && THEPL->items.p_elems[i] != playing )
            ids << THEPL->items.p_elems[i]->i_id;
    playlist_Unlock( THEPL );
    foreach( int id, ids )
        attachToItem( p_intf, id );
}

void SubtitleScope::applyToCurrent( intf_thread_t *p_intf )
{
    input_thread_t *p_input = THEMIM->getInput();
    if( !p_input )
        return;
    input_item_t *item = input_GetItem( p_input );
    const QStringList subs = existingSubs( p_intf, item );
    char *psz_uri = input_item_GetURI( item );
    const QString uri = qfu( psz_uri );
    free( psz_uri );
    /* Attached before the item started: VLC loaded and selected it. Otherwise
     * (queued before the interface existed) add it now. The item's own slave
     * flags can't tell: VLC's autodetection forces its own pick, and the
     * media library keeps flags from earlier sessions. VLC may then list an
     * autodetected copy twice, but the remembered one is the one shown. */
    if( subs.isEmpty() || handled.contains( uri ) )
        return;
    handled.insert( uri );
    for( int i = 0; i < subs.count(); i++ )
        input_AddSlave( p_input, SLAVE_TYPE_SPU, qtu( subs[i] ), i == 0, false, false );
}

QString SubtitleScope::playlistDir()
{
    char *psz = config_GetUserDir( VLC_DATA_DIR );
    QString dir = qfu( psz ) + "/playlists";
    free( psz );
    QDir().mkpath( dir );
    return dir;
}

bool SubtitleScope::writeXspf( const QString &path, const QString &title,
                               const QString &imageUri, const QStringList &uris )
{
    QFile f( path );
    if( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    QXmlStreamWriter x( &f );
    x.setAutoFormatting( true );
    x.writeStartDocument();
    x.writeStartElement( "playlist" );
    x.writeDefaultNamespace( "http://xspf.org/ns/0/" );
    x.writeAttribute( "version", "1" );
    x.writeTextElement( "title", title );
    if( !imageUri.isEmpty() )
        x.writeTextElement( "image", imageUri );
    x.writeStartElement( "trackList" );
    foreach( const QString &u, uris )
    {
        x.writeStartElement( "track" );
        x.writeTextElement( "location", u );
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndElement();
    x.writeEndDocument();
    return f.error() == QFile::NoError;
}
