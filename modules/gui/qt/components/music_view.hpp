/*****************************************************************************
 * music_view.hpp : the player's music mode (P5c, Figma row 2 #8-9)
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

#ifndef VLC_QT_MUSIC_VIEW_HPP_
#define VLC_QT_MUSIC_VIEW_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QPixmap>
#include <QTimer>
#include <QWidget>

class MediaLibrary;

/* Over the background widget while audio plays without video: the song's
 * art (736px at 1080, radius 16) centred on the window, a dark blur of
 * the same art behind it, and "title - artist" under it. */
class MusicView : public QWidget
{
    Q_OBJECT
public:
    MusicView( intf_thread_t *, QWidget *background );

    bool isActive() const { return active; }

signals:
    void activeChanged( bool );

protected:
    void paintEvent( QPaintEvent * ) override;
    bool eventFilter( QObject *, QEvent * ) override;
    void contextMenuEvent( QContextMenuEvent * ) override;

private:
    void refresh();            /* input, video or status changed */
    void loadArt();
    void setActive( bool );
    QRect artRect() const;
    QPixmap backdrop();

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    bool active = false;
    QTimer settle;             /* a video's output appears after its input */
    QString path;              /* the playing local file */
    QString vlcArt;            /* VLC's own art for the item, if any */
    QString caption;
    QImage art;
    QPixmap blurred;
    QSize blurredFor;
};

#endif
