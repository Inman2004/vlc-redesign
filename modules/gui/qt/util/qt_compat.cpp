/*****************************************************************************
 * qt_compat.cpp : the newer Qt calls the redesign uses, back to Qt 5.6
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

#include "util/qt_compat.hpp"

#include <QApplication>
#include <QDesktopWidget>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRawFont>
#include <QScreen>
#include <QThread>
#include <QWidget>
#include <QWindow>

#ifdef _WIN32
# include <windows.h>
# include <shellapi.h>
#endif

namespace
{
class FunctionThread : public QThread
{
public:
    explicit FunctionThread( std::function<void()> fn ) : f( std::move( fn ) ) {}
protected:
    void run() Q_DECL_OVERRIDE { f(); }
private:
    std::function<void()> f;
};

#if QT_VERSION < QT_VERSION_CHECK( 5, 15, 0 ) && defined( _WIN32 )
/* the native title-bar / border drag, as Qt 5.15 does it */
bool nativeDrag( QWidget *w, WPARAM hit )
{
    QWidget *top = w ? w->window() : NULL;
    if( !top )
        return false;
    HWND hwnd = reinterpret_cast<HWND>( top->winId() );
    if( !hwnd )
        return false;
    ReleaseCapture();
    POINT pt;
    GetCursorPos( &pt );
    PostMessageW( hwnd, WM_NCLBUTTONDOWN, hit, MAKELPARAM( pt.x, pt.y ) );
    return true;
}
#endif
}

int QtCompat::capHeight( const QFontMetrics &fm )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 8, 0 )
    return fm.capHeight();
#else
    /* the height of a flat capital, as the font draws it */
    return fm.tightBoundingRect( QStringLiteral( "H" ) ).height();
#endif
}

QScreen *QtCompat::screenAt( const QPoint &global )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
    return QGuiApplication::screenAt( global );
#else
    foreach( QScreen *s, QGuiApplication::screens() )
        if( s->geometry().contains( global ) )
            return s;
    return NULL;
#endif
}

QScreen *QtCompat::screenOf( const QWidget *w )
{
    if( !w )
        return QGuiApplication::primaryScreen();
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
    return w->screen();
#else
    const QWidget *top = w->window();
    if( top->windowHandle() && top->windowHandle()->screen() )
        return top->windowHandle()->screen();
    const int n = QApplication::desktop()->screenNumber( w );
    const QList<QScreen *> screens = QGuiApplication::screens();
    return n >= 0 && n < screens.size() ? screens.at( n ) : QGuiApplication::primaryScreen();
#endif
}

bool QtCompat::startSystemMove( QWidget *w )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
    QWidget *top = w ? w->window() : NULL;
    return top && top->windowHandle() && top->windowHandle()->startSystemMove();
#elif defined( _WIN32 )
    return nativeDrag( w, HTCAPTION );
#else
    Q_UNUSED( w );
    return false;
#endif
}

bool QtCompat::startSystemResize( QWidget *w, Qt::Edges edges )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
    QWidget *top = w ? w->window() : NULL;
    return top && top->windowHandle() && top->windowHandle()->startSystemResize( edges );
#elif defined( _WIN32 )
    WPARAM hit;
    const bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge, b = edges & Qt::BottomEdge;
    if( t && l )      hit = HTTOPLEFT;
    else if( t && r ) hit = HTTOPRIGHT;
    else if( b && l ) hit = HTBOTTOMLEFT;
    else if( b && r ) hit = HTBOTTOMRIGHT;
    else if( l )      hit = HTLEFT;
    else if( r )      hit = HTRIGHT;
    else if( t )      hit = HTTOP;
    else if( b )      hit = HTBOTTOM;
    else              return false;
    return nativeDrag( w, hit );
#else
    Q_UNUSED( w ); Q_UNUSED( edges );
    return false;
#endif
}

bool QtCompat::moveToTrash( const QString &path )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
    return QFile::moveToTrash( path );
#elif defined( _WIN32 )
    if( !QFileInfo::exists( path ) )
        return false;
    /* SHFileOperation wants a double-NUL terminated list */
    std::wstring from = QFileInfo( path ).absoluteFilePath()
                            .replace( '/', '\\' ).toStdWString();
    from.push_back( L'\0' );
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW( &op ) == 0 && !op.fAnyOperationsAborted;
#else
    Q_UNUSED( path );
    return false;
#endif
}

QThread *QtCompat::createThread( std::function<void()> f )
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
    return QThread::create( std::move( f ) );
#else
    return new FunctionThread( std::move( f ) );
#endif
}
