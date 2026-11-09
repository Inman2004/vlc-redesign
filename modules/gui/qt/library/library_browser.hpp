/*****************************************************************************
 * library_browser.hpp : browse state shared by the library page and the
 *                       player sidebar (P5a)
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

#ifndef VLC_QT_LIBRARY_BROWSER_HPP_
#define VLC_QT_LIBRARY_BROWSER_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QWidget;
class QMenu;
class MediaLibrary;

/* One tile or row: a library item, a group (a show, a season, the
 * "Shows" folder) that opens a deeper level, an action row of the
 * sidebar ("Add", "Add Network Stream", "Podcasts", a recent stream), or
 * the heading of a Home shelf ("Continue Watching ›"). */
struct LibraryEntry
{
    enum Type { ITEM, GROUP, ACTION, HEADER };
    /* Home tiles (Figma row 2 #7): 16:9 video, square art, square glyph */
    enum Tile { TILE_AUTO, TILE_WIDE, TILE_SQUARE, TILE_GLYPH };
    Type type = ITEM;
    Tile tile = TILE_AUTO;
    QString title;
    QString subtitle;       /* the second label line (the artist) */
    QString segment;        /* GROUP: the location segment it opens;
                               ACTION: the action key ("open:videos/Films",
                               "mrl:<uri>"); HEADER: the shelf key */
    QString icon;           /* ACTION, and GROUP rows in the sidebar */
    int index = -1;         /* ITEM: index in MediaLibrary::items() */
    QString thumbFrom;      /* the item whose thumbnail stands for it */
    int count = 0;          /* GROUP: items inside */
    bool watched = false;
    qreal progress = 0.0;
    qint64 date = 0;        /* newest file date, for sorting */
    int track = -1;         /* inside a named playlist: its position */
};

/* Where we are (videos > shows > The Wand > S01), the history, the search
 * text and the sort order, and what that shows. The library page and the
 * sidebar each own one; both paint LibraryEntry rows. */
class LibraryBrowser : public QObject
{
    Q_OBJECT
public:
    enum SortKey { UNWATCHED_FIRST, WATCHED_FIRST, TITLE_AZ, TITLE_ZA, NEWEST, OLDEST };

    /* settingsKey: where the sort order is kept ("Library/sort") */
    LibraryBrowser( intf_thread_t *, const QString &settingsKey, QObject *parent );

    static QString sectionTitle( const QString &key );

    /* the sidebar adds action rows ("Add", the network sources) */
    void setActionRows( bool b ) { actionRows = b; }

    const QStringList &location() const { return loc; }
    QString section() const { return loc.value( 0 ); }
    QString segmentTitle( int i ) const;
    void openSection( const QString &key );
    void setLocation( const QStringList &, bool record = true );
    bool canGoBack() const { return !backStack.isEmpty(); }
    bool canGoForward() const { return !fwdStack.isEmpty(); }
    void goBack();
    void goForward();

    void setSearch( const QString & );
    QString search() const { return needle; }
    int sortKey() const { return sort; }

    const QVector<LibraryEntry> &entries() const { return current; }
    /* rows that count as items (not the "Add..." rows nor headings) */
    int itemCount() const;
    /* the chip of the mini player: where playback was started from */
    QString sourceLabel() const;

    void activate( int entry );
    void showEntryMenu( int entry, const QPoint &globalPos, QWidget *parent );
    void showSortMenu( const QPoint &globalPos, QWidget *parent );
    /* asks for a folder and adds it to the library roots */
    static void addFolder( intf_thread_t *, QWidget *parent );

public slots:
    void rebuild();

signals:
    void locationChanged();
    void entriesChanged();
    /* something was started from here; first is the path playing */
    void played( const QString &first );
    /* an action row that is not handled here ("netstream", "podcast"...) */
    void actionRequested( const QString &key );

private:
    QVector<LibraryEntry> collect() const;
    QVector<LibraryEntry> homeEntries( const QString &shelf ) const;
    QVector<LibraryEntry> recentEntries() const;
    QVector<LibraryEntry> musicEntries() const;
    QVector<LibraryEntry> playlistEntries() const;
    void showPlaylistMenu( const QString &file, const QPoint &, QWidget *parent );
    void addToPlaylistMenu( QMenu *, const QStringList &uris, QWidget *parent );
    static QString shelfTitle( const QString &key );
    static QString topFolder( const QStringList &roots, const QString &path );
    void sortEntries( QVector<LibraryEntry> & ) const;
    void play( const QStringList &paths );

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    QString settingsKey;
    bool actionRows = false;
    QStringList loc;
    QList<QStringList> backStack, fwdStack;
    QString needle;
    int sort;
    QVector<LibraryEntry> current;
    QTimer refresh;
};

#endif
