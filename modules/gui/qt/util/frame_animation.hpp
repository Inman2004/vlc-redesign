/*****************************************************************************
 * frame_animation.hpp : animations timed to the display's refresh rate
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

#ifndef VLC_QT_FRAME_ANIMATION_HPP_
#define VLC_QT_FRAME_ANIMATION_HPP_

#include <QAbstractAnimation>
#include <QColor>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QVariant>
#include <QWidget>

class QVariantAnimation;

/* A QVariantAnimation look-alike that ticks at the screen's refresh rate.
 *
 * Qt 5 drives every QAbstractAnimation from one 16 ms (60 Hz) timer. On a
 * 100 or 120 Hz display those frames land unevenly on the refreshes and
 * motion judders. This one ticks once per refresh of the window's screen
 * and computes each value from the elapsed time, so a late tick never
 * slows the motion down, and the first value is applied at once. */
class FrameAnimation : public QObject
{
    Q_OBJECT
public:
    explicit FrameAnimation( QObject *parent );

    void setDuration( int ms ) { duration = qMax( 0, ms ); }
    int getDuration() const { return duration; }
    void setEasingCurve( const QEasingCurve & );
    void setStartValue( const QVariant & );
    void setEndValue( const QVariant & );
    QVariant startValue() const;
    QVariant endValue() const;
    QVariant currentValue() const;
    QAbstractAnimation::State state() const
    {
        return timer.isActive() ? QAbstractAnimation::Running : QAbstractAnimation::Stopped;
    }

    /* the timer interval for one frame on the widget's screen */
    static int refreshInterval( const QWidget * );

public slots:
    void start( QAbstractAnimation::DeletionPolicy = QAbstractAnimation::KeepWhenStopped );
    /* stops where it is; does not emit finished() */
    void stop();

signals:
    void valueChanged( const QVariant & );
    void finished();

private:
    void tick();
    int frameInterval() const;

    QVariantAnimation *curve;   /* interpolation only, never started */
    QTimer timer;
    QElapsedTimer clock;
    int duration = 250;
    bool deleteWhenStopped = false;
};

/* A still of a widget drawn over it, moved or faded without any layout:
 * the panel slides use it instead of resizing their content each frame. */
class SnapshotOverlay : public QWidget
{
    Q_OBJECT
public:
    /* covers `host` with `still`; `background` fills what the still leaves
     * uncovered (invalid: nothing, the host shows through) */
    SnapshotOverlay( QWidget *host, const QPixmap &still, const QColor &background );

    void setOffset( const QPointF &o ) { offset = o; update(); }
    QPointF getOffset() const { return offset; }
    void setStillOpacity( qreal o ) { opacity = o; update(); }

    /* the host as it looks now, at its current size */
    static QPixmap grabOf( QWidget * );

protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;

private:
    QPixmap still;
    QColor background;
    QPointF offset;
    qreal opacity = 1.0;
};

/* The slot right of the content that a side panel slides into.
 *
 * Opening takes the slot's full width in one step (the content and the
 * video resize once), then slides the panel in from the right edge;
 * closing slides it out, then gives the width back in one step. The
 * panel keeps its own width the whole time, so nothing re-flows. */
class SlideDock : public QWidget
{
    Q_OBJECT
public:
    enum { GAP = 10, DURATION = 220 };
    explicit SlideDock( QWidget *parent );
    bool isOpen() const { return open; }
    void setOpen( bool, bool animate = true );

protected:
    /* the panel, its fixed width; called once by the subclass */
    void setPanel( QWidget *, int width );
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;

private:
    void place();

    QWidget *panelWidget = NULL;
    int panelWidth = 0;
    FrameAnimation *anim;
    qreal shift = 0;    /* how far right of its resting place the panel is */
    bool open = false;
};

#endif
