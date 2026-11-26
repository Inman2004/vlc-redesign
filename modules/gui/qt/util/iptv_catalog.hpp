/*****************************************************************************
 * iptv_catalog.hpp : Live TV lists from iptv-org, channel lists and logos
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

#ifndef VLC_QT_IPTV_CATALOG_HPP_
#define VLC_QT_IPTV_CATALOG_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>

#include <functional>

class QThread;
typedef struct vlc_interrupt vlc_interrupt_t;

/* Downloads one URL with VLC's stream API (no dialogs) on its own
 * thread, interruptible */
class IptvFetch : public QObject
{
    Q_OBJECT
public:
    IptvFetch( intf_thread_t *, const QString &url, int maxBytes );
    ~IptvFetch();
    void start();
    void cancel();
signals:
    void fetched( const QByteArray &data );   /* empty on failure */
private:
    void run();
    intf_thread_t *p_intf;
    QString url;
    int maxBytes;
    vlc_interrupt_t *ctx;
    QThread *thread = NULL;
};

/* A list of channels or stations, by path:
 *   iptv-org:      "categories/news", "languages/tam", "countries/fr",
 *                  "index" (every channel)
 *   Radio Browser: "radio/top", "radio/language/tamil", "radio/country/IN"
 *   your own:      "url:<the M3U's address>" */
struct IptvList
{
    QString path;
    QString title;
    QString code;           /* countries: ISO 3166 code ("fr"; "gb" for iptv-org's "uk") */
    int count = -1;         /* channels, when iptv-org says */
};

/* A channel of a list, as its M3U describes it */
struct IptvChannel
{
    QString title;
    QString url;
    QString logo;           /* tvg-logo, may be empty */
    QString group;          /* group-title ("News;Public") */
    QStringList options;    /* #EXTVLCOPT lines (untrusted) */
};

/* The lists of github.com/iptv-org/iptv (its PLAYLISTS.md, fetched at
 * most once a day and kept in VLC's cache folder; built-in categories and
 * countries until then), the channels of a list once it is opened, and
 * channel logos. Everything is downloaded with VLC's own stream API on
 * worker threads: the Windows Qt has no network module. */
class IptvCatalog : public QObject
{
    Q_OBJECT
public:
    static IptvCatalog *instance( intf_thread_t * );
    static QString listUrl( const QString &path );
    /* a country's flag picture (flagcdn.com), for logo() */
    static QString flagUrl( const QString &code );

    QVector<IptvList> categories() const { return cats; }
    QVector<IptvList> languages() const { return langs; }
    QVector<IptvList> countries() const { return ctrs; }

    /* Radio Browser (radio-browser.info): station counts by language and
     * country, fetched once a day */
    QVector<IptvList> radioLanguages();
    QVector<IptvList> radioCountries();

    /* channel lists you added (any M3U address or file) */
    QVector<IptvList> myLists() const;
    void addMyList( const QString &title, const QString &url );
    void renameMyList( const QString &path, const QString &title );
    void removeMyList( const QString &path );

    /* the channels of a list; empty and *loading set while it downloads
     * (channelsReady follows), or after a failure (*failed set) */
    QVector<IptvChannel> channels( const QString &path, bool *loading = NULL,
                                   bool *failed = NULL );
    /* replaces the queue with the list, playing from channel `from` */
    void play( const QString &path, int from );

    /* a channel logo, fitted into size; null until it has downloaded
     * (logoReady follows). Only http(s) URLs are fetched. */
    QPixmap logo( const QString &url, const QSize &size, qreal dpr );

signals:
    void changed();                         /* the lists */
    void channelsReady( const QString &path );
    void logoReady( const QString &url );

private:
    IptvCatalog( intf_thread_t *, QObject *parent );
    ~IptvCatalog();
    void refreshLists();
    void parseLists( const QByteArray & );
    void builtInLists();
    void fetchRadioLists();
    QVector<IptvList> parseRadioLists( const QByteArray &, bool countries ) const;
    void fetch( const QString &url, int maxBytes, std::function<void( const QByteArray & )> done );
    void pumpLogos();

    intf_thread_t *p_intf;
    QString cacheDir;
    QVector<IptvList> cats, langs, ctrs, radioLangs, radioCtrs;
    bool radioFetching = false;
    QHash<QString, QVector<IptvChannel> > lists;
    QSet<QString> listsLoading, listsFailed;
    QHash<QString, QImage> logos;           /* decoded, by URL */
    QHash<QString, QPixmap> fitted;         /* scaled for a tile */
    QSet<QString> logosFailed;
    QStringList logoQueue;
    int logoJobs = 0;
    QList<IptvFetch *> jobs;
};

#endif
