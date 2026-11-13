/*****************************************************************************
 * qt_compat.hpp : the newer Qt calls the redesign uses, back to Qt 5.6
 *****************************************************************************
 * VLC 3.0's Windows contribs ship Qt 5.6.3; Linux builds use 5.15. Each
 * helper calls the Qt API when it exists and an equivalent otherwise.
 *
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

#ifndef VLC_QT_QT_COMPAT_HPP_
#define VLC_QT_QT_COMPAT_HPP_

#include <QtGlobal>
#include <QDateTime>
#include <QFontMetrics>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <utility>

class QScreen;
class QThread;
class QWidget;

#if QT_VERSION < QT_VERSION_CHECK( 5, 14, 0 ) && !defined( Q_MOC_RUN )
/* Qt 5.14 moved the split behaviour into the Qt namespace */
namespace Qt
{
    const QString::SplitBehavior SkipEmptyParts = QString::SkipEmptyParts;
    const QString::SplitBehavior KeepEmptyParts = QString::KeepEmptyParts;
}
#endif

namespace QtCompat
{
    /* QFontMetrics::horizontalAdvance (5.11) */
    template <class T>
    inline int textWidth( const QFontMetrics &fm, const T &text )
    {
#if QT_VERSION >= QT_VERSION_CHECK( 5, 11, 0 )
        return fm.horizontalAdvance( text );
#else
        return fm.width( text );
#endif
    }

    /* QFontMetrics::capHeight (5.8) */
    int capHeight( const QFontMetrics & );

    /* QGuiApplication::screenAt (5.10) and QWidget::screen (5.14) */
    QScreen *screenAt( const QPoint &global );
    QScreen *screenOf( const QWidget * );

    /* QWindow::startSystemMove / startSystemResize (5.15) on the top-level
     * window of the widget; true when the system took over the drag */
    bool startSystemMove( QWidget * );
    bool startSystemResize( QWidget *, Qt::Edges );

    /* QFile::moveToTrash (5.15) */
    bool moveToTrash( const QString &path );

    /* QDateTime::toSecsSinceEpoch / currentSecsSinceEpoch (5.8) */
    inline qint64 secsSinceEpoch( const QDateTime &d )
    {
        return d.toMSecsSinceEpoch() / 1000;
    }
    inline qint64 currentSecsSinceEpoch()
    {
        return QDateTime::currentMSecsSinceEpoch() / 1000;
    }

    /* QSet( first, last ) of a hash's keys (5.14) */
    template <class K, class V>
    inline QSet<K> keySet( const QHash<K, V> &h )
    {
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        return QSet<K>( h.keyBegin(), h.keyEnd() );
#else
        return QSet<K>::fromList( h.keys() );
#endif
    }

    /* QMetaObject::invokeMethod( context, functor, Qt::QueuedConnection )
     * (5.10): runs f on the context's thread; safe from any thread */
    template <class F>
    inline void invokeQueued( QObject *context, F &&f,
                              Qt::ConnectionType = Qt::QueuedConnection )
    {
        if( !context )
            return;
#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
        QMetaObject::invokeMethod( context, std::forward<F>( f ), Qt::QueuedConnection );
#else
        /* a queued connection on the source's destroyed() posts f to the
         * context's thread when the source goes out of scope */
        QObject source;
        QObject::connect( &source, &QObject::destroyed, context,
                          std::forward<F>( f ), Qt::QueuedConnection );
#endif
    }

    /* QThread::create (5.10) */
    QThread *createThread( std::function<void()> f );
}

#endif
