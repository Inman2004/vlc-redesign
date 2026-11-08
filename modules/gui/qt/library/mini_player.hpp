/*****************************************************************************
 * mini_player.hpp : the library window's mini player (P5a)
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

#ifndef VLC_QT_MINI_PLAYER_HPP_
#define VLC_QT_MINI_PLAYER_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>
#include <QPointer>
#include <QTimer>

class QLabel;
class QStackedLayout;
class QToolButton;
class QVBoxLayout;
class MiniProgress;

/* Figma row 2 #3-5: a 390x339 card over the library, bottom right
 * (floating) or bottom left (docked under the nav). The playing video is
 * moved into it while the library is shown; audio shows the art. Hovering
 * the picture shows previous / play / next, close and expand. */
class MiniPlayer : public QFrame
{
    Q_OBJECT
public:
    enum { CARD_W = 390, CARD_H = 339 };
    MiniPlayer( intf_thread_t *, QWidget *parent );

    /* the video widget is borrowed from the main window */
    void setVideo( QWidget * );
    QWidget *takeVideo();
    QWidget *video() const { return videoWidget; }

    bool isDocked() const { return docked; }
    void setDocked( bool );
    /* the playlist chip: where playback was started from */
    void setSourceLabel( const QString & );

signals:
    void expandRequested();     /* back to the player */
    void queueRequested();
    void dockChanged( bool );

protected:
    bool eventFilter( QObject *, QEvent * ) override;
    void showEvent( QShowEvent * ) override;
    void hideEvent( QHideEvent * ) override;
    void contextMenuEvent( QContextMenuEvent * ) override;

private:
    void updateInput();
    void updateArt();
    void setOverlayVisible( bool );
    void placeOverlay();
    void layoutText();

    intf_thread_t *p_intf;
    QWidget *picture;           /* the 16:9 area */
    QStackedLayout *pictureStack;
    QLabel *art;
    QWidget *videoHost;
    QVBoxLayout *videoLayout;
    QPointer<QWidget> videoWidget;
    QWidget *overlay;
    QToolButton *playBtn;
    MiniProgress *progress;
    QLabel *title, *folderChip, *sourceChip;
    QWidget *chipRow;
    bool docked = false;
    QTimer hoverPoll;
};

#endif
