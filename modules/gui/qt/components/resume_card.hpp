/*****************************************************************************
 * resume_card.hpp : "You already watched till ..." card (P5b)
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

#ifndef VLC_QT_RESUME_CARD_HPP_
#define VLC_QT_RESUME_CARD_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QWidget>
#include <QPointer>

class QLabel;

/* Figma Player frames, layer "continue": a 552x52 card centred over the
 * video, 24px above the control bar, replacing VLC's resume bar. The
 * video is a native window, so the card is a translucent tool window
 * that follows its owner (like the mini player's hover layer). */
class ResumeCard : public QWidget
{
    Q_OBJECT
public:
    ResumeCard( QWidget *window, QWidget *bar );

    void showFor( int64_t time );   /* the saved position, in microseconds */
    void place();

signals:
    void continueRequested();
    void dismissed();

protected:
    void paintEvent( QPaintEvent * ) override;
    bool eventFilter( QObject *, QEvent * ) override;

private:
    QWidget *window;
    QPointer<QWidget> bar;
    QLabel *text;
};

#endif
