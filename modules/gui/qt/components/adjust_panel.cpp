/*****************************************************************************
 * adjust_panel.cpp : redesigned adjustments sidebar (P3)
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

#include "components/adjust_panel.hpp"
#include "components/extended_panels.hpp"
#include "util/redesign_widgets.hpp"
#include "styles/vlc_theme.hpp"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"

#include <vlc_aout.h>
#include <vlc_vout.h>
#include <vlc_input.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QFontComboBox>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
#include <QSettings>
#include <QStyle>
#include <QCoreApplication>
#include <QDateTime>
#include <cmath>

/* Fixed geometry of the Figma "Subtitles" frame (400px panel) */
enum
{
    PANEL_MARGIN  = 20,  /* sliders and value boxes start at x 20 */
    TAB_BAR_H     = 50,
    VALUE_W       = 140,
    VALUE_H       = 36,
    ROW_SPACING   = 17,  /* 53px row pitch with 36px boxes */
};

/* Combo that refreshes its list right before it opens and leaves the
 * wheel to the panel unless it has focus. */
class PanelCombo : public QComboBox
{
public:
    explicit PanelCombo( QWidget *parent = NULL ) : QComboBox( parent )
    {
        setFocusPolicy( Qt::StrongFocus );
    }
    std::function<void()> aboutToShow;
    void showPopup() override
    {
        if( aboutToShow )
            aboutToShow();
        QComboBox::showPopup();
    }
protected:
    void wheelEvent( QWheelEvent *e ) override
    {
        if( !hasFocus() ) { e->ignore(); return; }
        QComboBox::wheelEvent( e );
    }
};

class PanelSpin : public QDoubleSpinBox
{
public:
    explicit PanelSpin( QWidget *parent = NULL ) : QDoubleSpinBox( parent )
    {
        setFocusPolicy( Qt::StrongFocus );
    }
protected:
    void wheelEvent( QWheelEvent *e ) override
    {
        if( !hasFocus() ) { e->ignore(); return; }
        QDoubleSpinBox::wheelEvent( e );
    }
};

class PanelIntSpin : public QSpinBox
{
public:
    explicit PanelIntSpin( QWidget *parent = NULL ) : QSpinBox( parent )
    {
        setFocusPolicy( Qt::StrongFocus );
    }
protected:
    void wheelEvent( QWheelEvent *e ) override
    {
        if( !hasFocus() ) { e->ignore(); return; }
        QSpinBox::wheelEvent( e );
    }
};

/**********************************************************************
 * Construction
 **********************************************************************/
AdjustPanel::AdjustPanel( intf_thread_t *_p_intf, QWidget *parent )
    : QWidget( parent ), p_intf( _p_intf ), b_refreshing( false ),
      videoTrack( NULL ), audioTrack( NULL ), subTrack( NULL ), audioDevice( NULL ),
      stereoMode( NULL ), aspectCombo( NULL ), cropCombo( NULL ), deintCombo( NULL ),
      deintModeCombo( NULL ), repeatCombo( NULL ), sleepCombo( NULL ), rateSpin( NULL ),
      audioDelay( NULL ), subDelay( NULL ), subFps( NULL ), subDuration( NULL ),
      shuffleSwitch( NULL ), stopAfterSwitch( NULL ), abButton( NULL ), lastSection( NULL ),
      rotateCombo( NULL )
{
    setObjectName( "VLCAdjust" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFocusPolicy( Qt::ClickFocus );

    saveTimer.setSingleShot( true );
    saveTimer.setInterval( 600 );
    connect( &saveTimer, &QTimer::timeout, this, [this]() {
        config_SaveConfigFile( p_intf );
    } );
    sleepTimer.setSingleShot( true );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* Tab strip: 50px #090909, icon 18 + 7 + text, 8px padding, 16px gaps,
     * a 2px accent underline that slides to the active tab */
    tabBar = new QWidget( this );
    tabBar->setObjectName( "AdjustTabs" );
    tabBar->setAttribute( Qt::WA_StyledBackground, true );
    tabBar->setFixedHeight( TAB_BAR_H );
    QHBoxLayout *tl = new QHBoxLayout( tabBar );
    tl->setContentsMargins( 12, 0, 12, 0 );
    tl->setSpacing( 10 );
    tabs = new QButtonGroup( this );
    tabs->setExclusive( true );
    const char *icons[] = { "playback", "video", "audio", "subtitles" };
    const QString names[] = { qtr( "Playback" ), qtr( "Video" ), qtr( "Audio" ), qtr( "Subtitles" ) };
    for( int i = 0; i < TAB_COUNT; i++ )
    {
        QToolButton *b = new QToolButton( tabBar );
        b->setCheckable( true );
        b->setText( names[i] );
        b->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        b->setIconSize( QSize( 18, 18 ) );
        QIcon ic;
        ic.addPixmap( Theme::pixmap( icons[i], 18, QColor( 0x40, 0x40, 0x40 ) ), QIcon::Normal, QIcon::Off );
        /* the active tab's glyph takes the accent, as in the Figma frame */
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Normal, QIcon::On );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::colors().textMuted ), QIcon::Active, QIcon::Off );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Active, QIcon::On );
        b->setIcon( ic );
        b->setFixedHeight( TAB_BAR_H );
        QFont f = b->font();
        f.setPixelSize( 14 );
        b->setFont( f );
        b->ensurePolished();
        b->setMinimumWidth( b->sizeHint().width() );
        b->setCursor( Qt::PointingHandCursor );
        b->setFocusPolicy( Qt::TabFocus );
        tabs->addButton( b, i );
        tl->addWidget( b );
    }
    tl->addStretch( 1 );
    underline = new QWidget( tabBar );
    underline->setObjectName( "AdjustTabLine" );
    underline->setAttribute( Qt::WA_StyledBackground, true );
    underlineAnim = new QVariantAnimation( this );
    underlineAnim->setDuration( 200 );
    underlineAnim->setEasingCurve( QEasingCurve::OutCubic );
    connect( underlineAnim, &QVariantAnimation::valueChanged, this, [this]( const QVariant &v ) {
        underline->setGeometry( v.toRect() );
    } );
    tabBar->installEventFilter( this );
    layout->addWidget( tabBar );

    stack = new QStackedWidget( this );
    stack->addWidget( buildPlaybackTab() );
    stack->addWidget( buildVideoTab() );
    stack->addWidget( buildAudioTab() );
    stack->addWidget( buildSubtitlesTab() );
    layout->addWidget( stack, 1 );
    layout->addWidget( buildFooter() );

    connect( tabs, QOverload<int>::of( &QButtonGroup::buttonClicked ), this,
             [this]( int id ) { setCurrentTab( id ); } );

    int last = getSettings()->value( "Adjust/tab", SUBTITLES_TAB ).toInt();
    setCurrentTab( qBound( 0, last, TAB_COUNT - 1 ), false );

    /* live state */
    connect( THEMIM, &MainInputManager::inputChanged, this, &AdjustPanel::refreshInput );
    connect( THEMIM, &MainInputManager::inputChanged, this, [this]( bool has ) {
        /* the audio output appears shortly after the input */
        if( has )
            QTimer::singleShot( 600, this, &AdjustPanel::refreshAudioEffects );
    } );
    connect( THEMIM->getIM(), &InputManager::synchroChanged, this, &AdjustPanel::refreshInput );
    connect( THEMIM->getIM(), &InputManager::voutChanged, this, &AdjustPanel::refreshVout );
    connect( THEMIM->getIM(), &InputManager::rateChanged, this, [this]( float r ) {
        if( !rateSpin ) return;
        QSignalBlocker b( rateSpin );
        rateSpin->setValue( r );
        if( AdjustSlider *s = rateSpin->property( "slider" ).value<AdjustSlider *>() )
        {
            QSignalBlocker bs( s );
            s->setValue( qRound( 100 * std::log2( r ) ) );
        }
    } );
    connect( THEMIM, &MainInputManager::randomChanged, this, [this]( bool b ) {
        if( shuffleSwitch ) { QSignalBlocker bl( shuffleSwitch ); shuffleSwitch->setCheckedAnimated( b ); }
    } );
    connect( THEMIM, &MainInputManager::repeatLoopChanged, this, [this]( int i ) {
        if( !repeatCombo ) return;
        QSignalBlocker bl( repeatCombo );
        repeatCombo->setCurrentIndex( i == REPEAT_ALL ? 1 : i == REPEAT_ONE ? 2 : 0 );
    } );
    connect( THEMIM->getIM(), &InputManager::AtoBchanged, this, [this]( bool a, bool b ) {
        if( !abButton ) return;
        abButton->setText( !a ? qtr( "Set A" ) : !b ? qtr( "Set B" ) : qtr( "Clear" ) );
        abButton->setProperty( "active", a );
        abButton->style()->unpolish( abButton );
        abButton->style()->polish( abButton );
    } );
    refreshInput();
}

int AdjustPanel::currentTab() const { return stack->currentIndex(); }

void AdjustPanel::setCurrentTab( int i, bool animate )
{
    if( i < 0 || i >= TAB_COUNT )
        return;
    tabs->button( i )->setChecked( true );
    const bool changed = stack->currentIndex() != i;
    stack->setCurrentIndex( i );
    getSettings()->setValue( "Adjust/tab", i );
    if( changed && i == AUDIO_TAB )
        refreshAudioEffects();
    moveUnderline( animate && isVisible() );

    if( changed && animate && isVisible() )
    {
        /* quick fade-in of the new page */
        QWidget *page = stack->currentWidget();
        QGraphicsOpacityEffect *fx = new QGraphicsOpacityEffect( page );
        page->setGraphicsEffect( fx );
        QVariantAnimation *a = new QVariantAnimation( page );
        a->setDuration( 140 );
        a->setStartValue( 0. );
        a->setEndValue( 1. );
        connect( a, &QVariantAnimation::valueChanged, fx, [fx]( const QVariant &v ) {
            fx->setOpacity( v.toReal() );
        } );
        connect( a, &QVariantAnimation::finished, page, [page]() {
            page->setGraphicsEffect( NULL );
        } );
        a->start( QAbstractAnimation::DeleteWhenStopped );
    }
}

void AdjustPanel::moveUnderline( bool animate )
{
    QAbstractButton *b = tabs->checkedButton();
    if( !b )
        return;
    const QRect target( b->x(), TAB_BAR_H - 2, b->width(), 2 );
    underlineAnim->stop();
    if( !animate || underline->width() <= 0 )
    {
        underline->setGeometry( target );
        return;
    }
    underlineAnim->setStartValue( underline->geometry() );
    underlineAnim->setEndValue( target );
    underlineAnim->start();
}

bool AdjustPanel::eventFilter( QObject *o, QEvent *e )
{
    if( e->type() == QEvent::Wheel && qobject_cast<QComboBox *>( o )
     && !static_cast<QWidget *>( o )->hasFocus() )
    {
        for( QWidget *w = static_cast<QWidget *>( o ); w; w = w->parentWidget() )
            if( QScrollArea *area = qobject_cast<QScrollArea *>( w ) )
            {
                QCoreApplication::sendEvent( area->verticalScrollBar(), e );
                break;
            }
        return true;
    }
    if( e->type() == QEvent::Resize )
        if( QScrollBar *bar = qobject_cast<QScrollBar *>(
                o->property( "overlayBar" ).value<QObject *>() ) )
        {
            QWidget *w = static_cast<QWidget *>( o );
            bar->setGeometry( w->width() - 8, 2, 6, w->height() - 4 );
            bar->raise();
        }
    if( o == tabBar && ( e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest
                      || e->type() == QEvent::Show ) )
        QTimer::singleShot( 0, this, [this]() { moveUnderline( false ); } );
    return QWidget::eventFilter( o, e );
}

void AdjustPanel::showEvent( QShowEvent *e )
{
    QWidget::showEvent( e );
    /* the bar's rotate button may have changed the transform */
    if( rotateCombo )
    {
        QSignalBlocker b( rotateCombo );
        rotateCombo->setCurrentIndex( VideoFilters::isEnabled( p_intf, "transform" )
            ? qMax( 0, rotateCombo->findData( VideoFilters::option( p_intf, "transform-type" ).toString() ) )
            : 0 );
    }
    refreshInput();
    refreshAudioEffects();
    QTimer::singleShot( 0, this, [this]() { moveUnderline( false ); } );
}

void AdjustPanel::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Escape )
    {
        emit closeRequested();
        e->accept();
        return;
    }
    if( e->modifiers() & Qt::ControlModifier
     && ( e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab ) )
    {
        const int d = e->key() == Qt::Key_Backtab ? TAB_COUNT - 1 : 1;
        setCurrentTab( ( currentTab() + d ) % TAB_COUNT );
        e->accept();
        return;
    }
    QWidget::keyPressEvent( e );
}

/**********************************************************************
 * Layout helpers
 **********************************************************************/
QScrollArea *AdjustPanel::makePage( QVBoxLayout **out )
{
    QScrollArea *area = new QScrollArea;
    area->setObjectName( "AdjustScroll" );
    area->setWidgetResizable( true );
    area->setFrameShape( QFrame::NoFrame );
    area->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    QWidget *page = new QWidget;
    page->setObjectName( "AdjustPage" );
    QVBoxLayout *l = new QVBoxLayout( page );
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 0 );
    area->setWidget( page );
    area->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

    /* Overlay scrollbar: drawn over the right edge instead of taking
     * 8px from the measured 20px margins */
    QScrollBar *overlay = new QScrollBar( Qt::Vertical, area );
    overlay->setObjectName( "AdjustOverlayBar" );
    overlay->hide();
    QScrollBar *real = area->verticalScrollBar();
    connect( real, &QScrollBar::rangeChanged, overlay, [overlay]( int min, int max ) {
        overlay->setRange( min, max );
        overlay->setVisible( max > min );
    } );
    connect( real, &QScrollBar::valueChanged, overlay, &QScrollBar::setValue );
    connect( overlay, &QScrollBar::valueChanged, real, &QScrollBar::setValue );
    overlay->setPageStep( real->pageStep() );
    connect( real, &QScrollBar::rangeChanged, overlay, [overlay, real]() {
        overlay->setPageStep( real->pageStep() );
        overlay->setSingleStep( real->singleStep() );
    } );
    area->setProperty( "overlayBar", QVariant::fromValue<QObject *>( overlay ) );
    area->installEventFilter( this );
    *out = l;
    return area;
}

QVBoxLayout *AdjustPanel::addSection( QVBoxLayout *page, const QString &title,
                                      ToggleSwitch **toggle )
{
    /* a section ending on a slider needs 24px under the track */
    if( lastSection && lastSection->count() > 0 )
        if( QLayoutItem *it = lastSection->itemAt( lastSection->count() - 1 ) )
            if( !it->widget() && lastSection->count() > 1
             && qobject_cast<AdjustSlider *>( lastSection->itemAt( lastSection->count() - 2 )->widget() ) )
                lastSection->addSpacing( 6 );
    if( page->count() > 0 )
    {
        QFrame *rule = new QFrame;
        rule->setObjectName( "AdjustRule" );
        rule->setFixedHeight( 1 );
        page->addWidget( rule );
    }
    QWidget *sec = new QWidget;
    sec->setObjectName( "AdjustSection" );
    QVBoxLayout *l = new QVBoxLayout( sec );
    /* 17px under the tab bar, 19px under a rule (Figma "Subtitles") */
    l->setContentsMargins( PANEL_MARGIN, page->count() > 1 ? 19 : 17, PANEL_MARGIN, 16 );
    l->setSpacing( ROW_SPACING );

    QHBoxLayout *head = new QHBoxLayout;
    head->setContentsMargins( 0, 0, 0, 0 );
    QLabel *t = new QLabel( title, sec );
    t->setObjectName( "AdjustSectionTitle" );
    head->addWidget( t );
    head->addStretch( 1 );
    if( toggle )
    {
        *toggle = new ToggleSwitch( sec );
        ( *toggle )->setToolTip( qtr( "Turn %1 on or off" ).arg( title ) );
        head->addWidget( *toggle );
    }
    l->addLayout( head );
    page->addWidget( sec );
    lastSection = l;
    return l;
}

QLabel *AdjustPanel::addRow( QVBoxLayout *sec, const QString &label, QWidget *right )
{
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins( 0, 0, 0, 0 );
    row->setSpacing( 8 );
    QLabel *l = new QLabel( label );
    l->setObjectName( "AdjustLabel" );
    l->setBuddy( right );
    row->addWidget( l, 1 );
    if( right )
    {
        if( !qobject_cast<ToggleSwitch *>( right ) && right->maximumWidth() == QWIDGETSIZE_MAX )
            right->setFixedSize( VALUE_W, VALUE_H );
        row->addWidget( right );
    }
    sec->addLayout( row );
    return l;
}

QComboBox *AdjustPanel::makeCombo( std::function<void()> aboutToShow )
{
    PanelCombo *c = new PanelCombo;
    c->setObjectName( "AdjustCombo" );
    c->aboutToShow = aboutToShow;
    c->setCursor( Qt::PointingHandCursor );
    return c;
}

QDoubleSpinBox *AdjustPanel::makeSpin( double min, double max, int decimals,
                                       double step, const QString &suffix )
{
    PanelSpin *s = new PanelSpin;
    s->setObjectName( "AdjustSpin" );
    s->setButtonSymbols( QAbstractSpinBox::NoButtons );
    s->setRange( min, max );
    s->setDecimals( decimals );
    s->setSingleStep( step );
    s->setSuffix( suffix );
    s->setKeyboardTracking( false );
    s->setAccelerated( true );
    return s;
}

AdjustSlider *AdjustPanel::bindSlider( QVBoxLayout *sec, QDoubleSpinBox *spin,
                                       double min, double max, double origin, int ticks )
{
    const double scale = std::pow( 10., spin->decimals() );
    AdjustSlider *s = new AdjustSlider;
    s->setRange( qRound( min * scale ), qRound( max * scale ) );
    s->setOrigin( qRound( origin * scale ) );
    s->setResetValue( qRound( origin * scale ) );
    s->setTicks( ticks );
    s->setSingleStep( qMax( 1, qRound( spin->singleStep() * scale ) ) );
    s->setPageStep( s->singleStep() * 10 );
    s->setValue( qRound( spin->value() * scale ) );
    connect( s, &QSlider::valueChanged, spin, [spin, scale]( int v ) {
        spin->setValue( v / scale );
    } );
    connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), s, [s, scale]( double d ) {
        QSignalBlocker b( s );
        s->setValue( qRound( d * scale ) );
    } );
    spin->setProperty( "slider", QVariant::fromValue( s ) );
    /* 19px between a box and the 16px track (the widget is 24 tall) */
    sec->addSpacing( -2 );
    sec->addWidget( s );
    sec->addSpacing( -2 );
    return s;
}

void AdjustPanel::addScale( QVBoxLayout *sec, const QStringList &labels )
{
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins( 0, 0, 0, 0 );
    for( int i = 0; i < labels.size(); i++ )
    {
        QLabel *l = new QLabel( labels[i] );
        l->setObjectName( "AdjustScale" );
        l->setAlignment( i == 0 ? Qt::AlignLeft
                       : i == labels.size() - 1 ? Qt::AlignRight : Qt::AlignHCenter );
        row->addWidget( l, 1 );
    }
    /* pull the scale up under its slider (the section spacing is for rows) */
    sec->addSpacing( 8 - ROW_SPACING );
    sec->addLayout( row );
}

CollapsibleSection *AdjustPanel::addCard( QVBoxLayout *sec, const QString &key )
{
    CollapsibleSection *card = new CollapsibleSection;
    sec->addWidget( card );
    const QString k = "Adjust/expanded/" + key;
    card->setExpanded( getSettings()->value( k, false ).toBool(), false );
    connect( card, &CollapsibleSection::expandedChanged, this, [this, k]( bool b ) {
        getSettings()->setValue( k, b );
    } );
    return card;
}

/* "Label        value" + slider for one filter option. A master switch
 * greys the slider out; zeroDisables turns the filter itself on only
 * while the value is above zero (sharpen, grain, banding, sepia). */
void AdjustPanel::addOptionSlider( QVBoxLayout *card, const QString &label,
                                   const char *module, const char *option,
                                   double min, double max, double def,
                                   int ticks, ToggleSwitch *master, bool zeroDisables )
{
    const bool isFloat = ( config_GetType( option ) & VLC_VAR_CLASS ) == VLC_VAR_FLOAT;
    const double scale = !isFloat ? 1. : ( max - min ) > 50 ? 10. : 100.;

    if( card->count() > 0 )
        card->addSpacing( 6 );
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins( 0, 0, 0, 0 );
    QLabel *name = new QLabel( label );
    name->setObjectName( "AdjustLabel" );
    QLabel *value = new QLabel;
    value->setObjectName( "AdjustInlineValue" );
    value->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    row->addWidget( name );
    row->addStretch( 1 );
    row->addWidget( value );
    card->addLayout( row );

    AdjustSlider *s = new AdjustSlider;
    s->setRange( qRound( min * scale ), qRound( max * scale ) );
    s->setResetValue( qRound( def * scale ) );
    if( min < 0 && max > 0 )
        s->setOrigin( 0 );
    s->setTicks( ticks );
    s->setPageStep( qMax( 1, ( s->maximum() - s->minimum() ) / 10 ) );
    card->addWidget( s );

    QVariant cur = VideoFilters::option( p_intf, option );
    double v = cur.isValid() ? cur.toDouble() : def;
    if( zeroDisables && !VideoFilters::isEnabled( p_intf, module ) )
        v = 0;
    s->setValue( qRound( v * scale ) );

    const QByteArray mod( module ), opt( option );
    auto text = [value, scale, isFloat]( int i ) {
        value->setText( isFloat ? QString::number( i / scale, 'f', scale >= 100 ? 2 : 1 )
                                : QString::number( i ) );
    };
    text( s->value() );
    auto syncFilter = [this, mod, s, master]() {
        const bool on = s->value() > 0 && ( !master || master->isChecked() );
        if( on != VideoFilters::isEnabled( p_intf, mod.constData() ) )
            VideoFilters::setEnabled( p_intf, mod.constData(), on );
    };
    connect( s, &QSlider::valueChanged, this, [=]( int i ) {
        text( i );
        if( b_refreshing )
            return;
        if( isFloat )
            VideoFilters::setOption( p_intf, mod.constData(), opt.constData(), i / scale );
        else
            VideoFilters::setOption( p_intf, mod.constData(), opt.constData(),
                                     /* gradfun needs a radius of 4 or more */
                                     QVariant( qMax( i, opt == "gradfun-radius" ? 4 : i ) ) );
        if( zeroDisables )
            syncFilter();
    } );
    if( master )
    {
        s->setEnabled( master->isChecked() );
        connect( master, &ToggleSwitch::toggled, s, [=]( bool on ) {
            s->setEnabled( on );
            if( zeroDisables )
                syncFilter();
        } );
    }
    registerControl( QString( "video/%1" ).arg( option ),
                     [s]() { return QVariant( s->value() ); },
                     [s]( const QVariant &v ) { s->setValue( v.toInt() ); },
                     QVariant( zeroDisables ? 0 : qRound( def * scale ) ) );
}

void AdjustPanel::addAudioEffect( QVBoxLayout *page, const QString &title,
                                  AudioFilterControlWidget *w )
{
    ToggleSwitch *t;
    QVBoxLayout *sec = addSection( page, title, &t );
    t->setChecked( w->isFilterEnabled() );
    w->setEnabled( t->isChecked() );
    connect( t, &ToggleSwitch::toggled, w, [w]( bool on ) {
        w->setFilterEnabled( on );
        w->setEnabled( on );
    } );
    audioEffects << qMakePair( t, w );
    CollapsibleSection *card = addCard( sec, w->filterName() );
    card->bodyLayout()->addWidget( w );

    const QString key = "audio/" + w->filterName();
    /* setChecked() emits toggled(), which keeps the filter chain in step */
    registerControl( key + "/on", [t]() { return QVariant( t->isChecked() ); },
                     [t]( const QVariant &v ) { t->setCheckedAnimated( v.toBool() ); },
                     QVariant( false ) );
    int i = 0;
    foreach( AdjustSlider *s, w->findChildren<AdjustSlider *>() )
    {
        const int def = s->resetTarget();
        registerControl( QString( "%1/%2" ).arg( key ).arg( i++ ),
                         [s]() { return QVariant( s->value() ); },
                         [s]( const QVariant &v ) { s->setValue( v.toInt() ); },
                         QVariant( def ) );
    }
}

/**********************************************************************
 * VLC helpers
 **********************************************************************/
void AdjustPanel::fillTrackCombo( QComboBox *combo, const char *var )
{
    QSignalBlocker b( combo );
    combo->clear();
    input_thread_t *p_input = THEMIM->getInput();
    if( p_input )
    {
        vlc_value_t val, text;
        if( var_Change( p_input, var, VLC_VAR_GETCHOICES, &val, &text ) == VLC_SUCCESS )
        {
            for( int i = 0; i < val.p_list->i_count; i++ )
                combo->addItem( qfu( text.p_list->p_values[i].psz_string ),
                                QVariant( (qlonglong) val.p_list->p_values[i].i_int ) );
            var_FreeList( &val, &text );
        }
        combo->setCurrentIndex( combo->findData( (qlonglong) var_GetInteger( p_input, var ) ) );
    }
    if( combo->count() == 0 )
        combo->addItem( qtr( "None" ), QVariant( (qlonglong) -1 ) );
    combo->setEnabled( p_input != NULL && combo->count() > 1 );
}

void AdjustPanel::setVoutVar( const char *var, const QVariant &v )
{
    int type = config_GetType( var ) & VLC_VAR_CLASS;
    if( type == 0 )
        type = v.type() == QVariant::String ? VLC_VAR_STRING : VLC_VAR_INTEGER;
    const QByteArray str = v.toString().toUtf8();
    vlc_value_t val;
    switch( type )
    {
    case VLC_VAR_BOOL:   val.b_bool = v.toBool(); break;
    case VLC_VAR_FLOAT:  val.f_float = v.toFloat(); break;
    case VLC_VAR_STRING: val.psz_string = (char *) str.constData(); break;
    default:             val.i_int = v.toLongLong(); type = VLC_VAR_INTEGER; break;
    }
    /* on the playlist so the next video inherits it... */
    var_Create( THEPL, var, type | VLC_VAR_DOINHERIT );
    if( ( var_Type( THEPL, var ) & VLC_VAR_CLASS ) == type )
        var_SetChecked( THEPL, var, type, val );
    /* ...and live on the current ones */
    foreach( vout_thread_t *p_vout, THEMIM->getVouts() )
    {
        if( ( var_Type( p_vout, var ) & VLC_VAR_CLASS ) == type )
            var_SetChecked( p_vout, var, type, val );
        vlc_object_release( p_vout );
    }
}

/* freetype and subsdec read these through var_Inherit*, i.e. from the
 * configuration: write it there and save shortly after. */
void AdjustPanel::setSubtitleConfig( const char *option, const QVariant &v )
{
    if( b_refreshing )
        return;
    switch( config_GetType( option ) & VLC_VAR_CLASS )
    {
    case VLC_VAR_STRING: config_PutPsz( p_intf, option, qtu( v.toString() ) ); break;
    case VLC_VAR_FLOAT:  config_PutFloat( p_intf, option, v.toFloat() ); break;
    case VLC_VAR_BOOL:
    case VLC_VAR_INTEGER: config_PutInt( p_intf, option, v.toLongLong() ); break;
    default: return;
    }
    saveTimer.start();
}

/**********************************************************************
 * Playback
 **********************************************************************/
QWidget *AdjustPanel::buildPlaybackTab()
{
    QVBoxLayout *page;
    QScrollArea *area = makePage( &page );

    /* Speed: log scale so 0.5x and 2x sit symmetric around 1x */
    QVBoxLayout *sec = addSection( page, qtr( "Speed" ) );
    rateSpin = makeSpin( 0.25, 4.0, 2, 0.05, QString::fromUtf8( "×" ) );
    rateSpin->setValue( var_InheritFloat( THEPL, "rate" ) );
    addRow( sec, qtr( "Playback Speed" ), rateSpin );
    AdjustSlider *rs = new AdjustSlider;
    rs->setRange( -200, 200 );
    rs->setOrigin( 0 );
    rs->setResetValue( 0 );
    rs->setTicks( 4 );
    rs->setPageStep( 50 );
    rs->setValue( qRound( 100 * std::log2( rateSpin->value() ) ) );
    rateSpin->setProperty( "slider", QVariant::fromValue( rs ) );
    sec->addSpacing( -2 );
    sec->addWidget( rs );
    sec->addSpacing( -2 );
    addScale( sec, QStringList() << QString::fromUtf8( "0.25×" )
                                 << QString::fromUtf8( "1×" )
                                 << QString::fromUtf8( "4×" ) );
    connect( rs, &QSlider::valueChanged, rateSpin, [this]( int v ) {
        /* snap to 1x near the middle */
        double r = std::abs( v ) < 4 ? 1.0 : std::pow( 2., v / 100. );
        rateSpin->setValue( r );
    } );
    connect( rateSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this, rs]( double r ) {
        { QSignalBlocker b( rs ); rs->setValue( qRound( 100 * std::log2( r ) ) ); }
        if( !b_refreshing )
            var_SetFloat( THEPL, "rate", r );
    } );
    registerControl( "playback/rate", [this]() { return QVariant( rateSpin->value() ); },
                     [this]( const QVariant &v ) { rateSpin->setValue( v.toDouble() ); }, QVariant( 1.0 ) );

    /* Playlist behaviour */
    sec = addSection( page, qtr( "Playlist" ) );
    repeatCombo = makeCombo();
    repeatCombo->addItems( QStringList() << qtr( "Off" ) << qtr( "All" ) << qtr( "One" ) );
    repeatCombo->setCurrentIndex( var_GetBool( THEPL, "loop" ) ? 1 : var_GetBool( THEPL, "repeat" ) ? 2 : 0 );
    connect( repeatCombo, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        var_SetBool( THEPL, "loop", i == 1 );
        var_SetBool( THEPL, "repeat", i == 2 );
        config_PutInt( p_intf, "loop", i == 1 );
        config_PutInt( p_intf, "repeat", i == 2 );
    } );
    addRow( sec, qtr( "Repeat" ), repeatCombo );

    shuffleSwitch = new ToggleSwitch;
    shuffleSwitch->setChecked( var_GetBool( THEPL, "random" ) );
    connect( shuffleSwitch, &ToggleSwitch::clicked, this, [this]( bool on ) {
        var_SetBool( THEPL, "random", on );
        config_PutInt( p_intf, "random", on );
    } );
    addRow( sec, qtr( "Shuffle" ), shuffleSwitch );

    stopAfterSwitch = new ToggleSwitch;
    stopAfterSwitch->setChecked( var_GetBool( THEPL, "play-and-stop" ) );
    stopAfterSwitch->setToolTip( qtr( "Stop when the current item ends" ) );
    connect( stopAfterSwitch, &ToggleSwitch::clicked, this, [this]( bool on ) {
        var_SetBool( THEPL, "play-and-stop", on );
    } );
    addRow( sec, qtr( "Stop After Current" ), stopAfterSwitch );

    /* Loop and timer */
    sec = addSection( page, qtr( "Loop and Timer" ) );
    abButton = new QToolButton;
    abButton->setObjectName( "AdjustButton" );
    abButton->setText( qtr( "Set A" ) );
    abButton->setCursor( Qt::PointingHandCursor );
    abButton->setToolTip( qtr( "Click to set point A, again for point B, a third time to clear" ) );
    connect( abButton, &QToolButton::clicked, THEMIM->getIM(), &InputManager::setAtoB );
    addRow( sec, qtr( "A-B Loop" ), abButton );

    sleepCombo = makeCombo();
    sleepCombo->addItem( qtr( "Off" ), 0 );
    foreach( int m, QList<int>() << 15 << 30 << 45 << 60 << 90 )
        sleepCombo->addItem( qtr( "%1 min" ).arg( m ), m );
    sleepCombo->addItem( qtr( "End of item" ), -1 );
    connect( &sleepTimer, &QTimer::timeout, this, [this]() {
        THEMIM->pause();
        QSignalBlocker b( sleepCombo );
        sleepCombo->setCurrentIndex( 0 );
        sleepCombo->setToolTip( QString() );
    } );
    connect( sleepCombo, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        const int m = sleepCombo->itemData( i ).toInt();
        sleepTimer.stop();
        sleepCombo->setToolTip( QString() );
        if( m > 0 )
        {
            sleepTimer.start( m * 60 * 1000 );
            sleepCombo->setToolTip( qtr( "Pauses at %1" ).arg(
                QDateTime::currentDateTime().addSecs( m * 60 ).toString( "HH:mm" ) ) );
        }
        else if( m < 0 )
        {
            var_SetBool( THEPL, "play-and-stop", true );
            stopAfterSwitch->setCheckedAnimated( true );
            QSignalBlocker b( sleepCombo );
            sleepCombo->setCurrentIndex( 0 );
        }
    } );
    addRow( sec, qtr( "Sleep Timer" ), sleepCombo );

    /* Resume */
    sec = addSection( page, qtr( "Resume" ) );
    QComboBox *cont = makeCombo();
    cont->addItems( QStringList() << qtr( "Never" ) << qtr( "Ask" ) << qtr( "Always" ) );
    cont->setCurrentIndex( qBound( 0, (int) var_InheritInteger( p_intf, "qt-continue" ), 2 ) );
    cont->setToolTip( qtr( "Continue playback where you left off" ) );
    connect( cont, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        config_PutInt( p_intf, "qt-continue", i );
        saveTimer.start();
    } );
    addRow( sec, qtr( "Continue Playback" ), cont );

    QToolButton *info = new QToolButton;
    info->setObjectName( "AdjustButton" );
    info->setText( qtr( "Show" ) );
    info->setCursor( Qt::PointingHandCursor );
    info->setToolTip( qtr( "Media information (Ctrl+I)" ) );
    connect( info, &QToolButton::clicked, THEDP, &DialogsProvider::mediaInfoDialog );
    addRow( sec, qtr( "Media Information" ), info );

    page->addStretch( 1 );
    return area;
}

/**********************************************************************
 * Video
 **********************************************************************/
static void addChoices( QComboBox *c, const QStringList &values, const QStringList &texts )
{
    for( int i = 0; i < values.size(); i++ )
        c->addItem( texts.value( i, values[i] ), values[i] );
}

QWidget *AdjustPanel::buildVideoTab()
{
    QVBoxLayout *page;
    QScrollArea *area = makePage( &page );

    QVBoxLayout *sec = addSection( page, qtr( "Defaults" ) );
    videoTrack = makeCombo( [this]() { fillTrackCombo( videoTrack, "video-es" ); } );
    connect( videoTrack, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        if( THEMIM->getInput() )
            var_SetInteger( THEMIM->getInput(), "video-es", videoTrack->itemData( i ).toLongLong() );
    } );
    addRow( sec, qtr( "Video Track" ), videoTrack );

    const QStringList ratios = QStringList() << "16:9" << "4:3" << "1:1" << "16:10"
                                             << "2.21:1" << "2.35:1" << "2.39:1" << "5:4";
    aspectCombo = makeCombo();
    aspectCombo->addItem( qtr( "Default" ), QString() );
    addChoices( aspectCombo, ratios, ratios );
    cropCombo = makeCombo();
    cropCombo->addItem( qtr( "None" ), QString() );
    addChoices( cropCombo, ratios, ratios );
    struct { QComboBox *c; const char *var; } ratioVars[] = {
        { aspectCombo, "aspect-ratio" }, { cropCombo, "crop" } };
    for( auto &rv : ratioVars )
    {
        QComboBox *c = rv.c;
        const char *var = rv.var;
        const int idx = c->findData( VideoFilters::option( p_intf, var ).toString() );
        c->setCurrentIndex( qMax( 0, idx ) );
        connect( c, QOverload<int>::of( &QComboBox::activated ), this, [this, c, var]( int i ) {
            setVoutVar( var, c->itemData( i ).toString() );
        } );
        registerControl( QString( "video/%1" ).arg( var ),
                         [c]() { return c->currentData(); },
                         [this, c, var]( const QVariant &v ) {
                             c->setCurrentIndex( qMax( 0, c->findData( v.toString() ) ) );
                             setVoutVar( var, c->currentData().toString() ); },
                         QVariant( QString() ) );
    }
    addRow( sec, qtr( "Aspect Ratio" ), aspectCombo );
    addRow( sec, qtr( "Crop Ratio" ), cropCombo );

    deintCombo = makeCombo();
    deintCombo->addItem( qtr( "Off" ), 0 );
    deintCombo->addItem( qtr( "Automatic" ), -1 );
    deintCombo->addItem( qtr( "On" ), 1 );
    deintCombo->setCurrentIndex( qMax( 0, deintCombo->findData(
        (int) var_InheritInteger( p_intf, "deinterlace" ) ) ) );
    connect( deintCombo, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        setVoutVar( "deinterlace", deintCombo->itemData( i ) );
        deintModeCombo->setEnabled( deintCombo->itemData( i ).toInt() != 0 );
    } );
    addRow( sec, qtr( "Deinterlace" ), deintCombo );

    deintModeCombo = makeCombo();
    {
        char **vals, **texts;
        ssize_t n = config_GetPszChoices( VLC_OBJECT( p_intf ), "deinterlace-mode", &vals, &texts );
        for( ssize_t i = 0; i < n; i++ )
        {
            deintModeCombo->addItem( qtr( texts[i] ), qfu( vals[i] ) );
            free( vals[i] ); free( texts[i] );
        }
        if( n > 0 ) { free( vals ); free( texts ); }
        char *cur = var_InheritString( p_intf, "deinterlace-mode" );
        deintModeCombo->setCurrentIndex( qMax( 0, deintModeCombo->findData( qfu( cur ) ) ) );
        free( cur );
    }
    deintModeCombo->setEnabled( deintCombo->currentData().toInt() != 0 );
    connect( deintModeCombo, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        setVoutVar( "deinterlace-mode", deintModeCombo->itemData( i ).toString() );
    } );
    addRow( sec, qtr( "Deinterlace Mode" ), deintModeCombo );

    QComboBox *rotate = makeCombo();
    rotateCombo = rotate;
    rotate->addItem( qtr( "None" ), QString() );
    {
        char **vals, **texts;
        ssize_t n = config_GetPszChoices( VLC_OBJECT( p_intf ), "transform-type", &vals, &texts );
        for( ssize_t i = 0; i < n; i++ )
        {
            rotate->addItem( qtr( texts[i] ), qfu( vals[i] ) );
            free( vals[i] ); free( texts[i] );
        }
        if( n > 0 ) { free( vals ); free( texts ); }
    }
    if( VideoFilters::isEnabled( p_intf, "transform" ) )
        rotate->setCurrentIndex( qMax( 0, rotate->findData(
            VideoFilters::option( p_intf, "transform-type" ).toString() ) ) );
    auto applyRotate = [this, rotate]() {
        const QString t = rotate->currentData().toString();
        VideoFilters::setEnabled( p_intf, "transform", false );
        if( !t.isEmpty() )
        {
            VideoFilters::setOption( p_intf, "transform", "transform-type", t );
            VideoFilters::setEnabled( p_intf, "transform", true );
        }
    };
    connect( rotate, QOverload<int>::of( &QComboBox::activated ), this, applyRotate );
    registerControl( "video/transform", [rotate]() { return rotate->currentData(); },
                     [rotate, applyRotate]( const QVariant &v ) {
                         rotate->setCurrentIndex( qMax( 0, rotate->findData( v.toString() ) ) );
                         applyRotate(); }, QVariant( QString() ) );
    addRow( sec, qtr( "Rotate and Flip" ), rotate );

    /* Image adjust (adjust + sharpen + gradfun + grain), Figma card 4 */
    ToggleSwitch *adj;
    sec = addSection( page, qtr( "Image Adjust" ), &adj );
    adj->setChecked( VideoFilters::isEnabled( p_intf, "adjust" ) );
    connect( adj, &ToggleSwitch::toggled, this, [this]( bool on ) {
        VideoFilters::setEnabled( p_intf, "adjust", on );
    } );
    registerControl( "video/adjust", [adj]() { return QVariant( adj->isChecked() ); },
                     [adj]( const QVariant &v ) { adj->setCheckedAnimated( v.toBool() ); }, false );
    QVBoxLayout *card = addCard( sec, "image" )->bodyLayout();
    addOptionSlider( card, qtr( "Hue" ),        "adjust", "hue",        -180, 180, 0, 0, adj );
    addOptionSlider( card, qtr( "Brightness" ), "adjust", "brightness", 0, 2, 1, 0, adj );
    addOptionSlider( card, qtr( "Contrast" ),   "adjust", "contrast",   0, 2, 1, 0, adj );
    addOptionSlider( card, qtr( "Saturation" ), "adjust", "saturation", 0, 3, 1, 3, adj );
    addOptionSlider( card, qtr( "Gamma" ),      "adjust", "gamma",      0.01, 10, 1, 10, adj );
    addOptionSlider( card, qtr( "Sharpen Sigma" ), "sharpen", "sharpen-sigma", 0, 2, 0, 4, adj, true );
    addOptionSlider( card, qtr( "Banding Removal Radius" ), "gradfun", "gradfun-radius", 0, 32, 0, 7, adj, true );
    addOptionSlider( card, qtr( "Film Grain Variance" ), "grain", "grain-variance", 0, 10, 0, 10, adj, true );

    /* Crop in pixels, Figma card 5: Top/Bottom and Left/Right with links */
    sec = addSection( page, qtr( "Crop" ) );
    CollapsibleSection *cropCard = addCard( sec, "crop" );
    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins( 0, 0, 0, 0 );
    grid->setHorizontalSpacing( 8 );
    grid->setVerticalSpacing( 12 );
    cropCard->bodyLayout()->addLayout( grid );
    const char *cropVars[] = { "crop-top", "crop-bottom", "crop-left", "crop-right" };
    const QString cropNames[] = { qtr( "Top" ), qtr( "Bottom" ), qtr( "Left" ), qtr( "Right" ) };
    QSpinBox *cropSpins[4];
    for( int i = 0; i < 4; i++ )
    {
        QWidget *box = new QWidget;
        box->setObjectName( "AdjustCropBox" );
        box->setAttribute( Qt::WA_StyledBackground, true );
        box->setFixedHeight( 32 );
        QHBoxLayout *bl = new QHBoxLayout( box );
        bl->setContentsMargins( 12, 0, 6, 0 );
        bl->setSpacing( 6 );
        QLabel *n = new QLabel( cropNames[i] );
        n->setObjectName( "AdjustLabel" );
        PanelIntSpin *sp = new PanelIntSpin;
        sp->setObjectName( "AdjustCropSpin" );
        sp->setButtonSymbols( QAbstractSpinBox::NoButtons );
        sp->setRange( 0, 4096 );
        sp->setSuffix( " px" );
        sp->setFrame( false );
        sp->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
        n->setBuddy( sp );
        bl->addWidget( n );
        bl->addWidget( sp, 1 );
        grid->addWidget( box, i / 2, i % 2 );
        cropSpins[i] = sp;
    }
    for( int r = 0; r < 2; r++ )
    {
        QToolButton *link = new QToolButton;
        link->setCheckable( true );
        link->setToolTip( r == 0 ? qtr( "Crop top and bottom equally" )
                                 : qtr( "Crop left and right equally" ) );
        QIcon ic;
        ic.addPixmap( Theme::pixmap( "link", 20, Theme::colors().textFaint ), QIcon::Normal, QIcon::Off );
        ic.addPixmap( Theme::pixmap( "link", 20, Theme::accent() ), QIcon::Normal, QIcon::On );
        link->setIcon( ic );
        link->setIconSize( QSize( 20, 20 ) );
        link->setObjectName( "AdjustLink" );
        link->setFixedSize( 28, 28 );
        grid->addWidget( link, r, 2 );
        QSpinBox *a = cropSpins[r * 2], *b = cropSpins[r * 2 + 1];
        connect( a, QOverload<int>::of( &QSpinBox::valueChanged ), b, [link, b]( int v ) {
            if( link->isChecked() ) b->setValue( v ); } );
        connect( b, QOverload<int>::of( &QSpinBox::valueChanged ), a, [link, a]( int v ) {
            if( link->isChecked() ) a->setValue( v ); } );
        connect( link, &QToolButton::toggled, a, [a, b]( bool on ) {
            if( on ) b->setValue( a->value() ); } );
    }
    for( int i = 0; i < 4; i++ )
    {
        QSpinBox *sp = cropSpins[i];
        const char *var = cropVars[i];
        connect( sp, QOverload<int>::of( &QSpinBox::valueChanged ), this, [this, var]( int v ) {
            setVoutVar( var, v );
        } );
        voutAppliers << [this, sp, var]() { if( sp->value() ) setVoutVar( var, sp->value() ); };
        registerControl( QString( "video/%1" ).arg( var ), [sp]() { return QVariant( sp->value() ); },
                         [sp]( const QVariant &v ) { sp->setValue( v.toInt() ); }, 0 );
    }

    /* Colours, Figma card 7 */
    sec = addSection( page, qtr( "Colors" ) );
    card = addCard( sec, "colors" )->bodyLayout();
    auto toggleRow = [this, card]( const QString &label, QWidget *extra ) -> ToggleSwitch * {
        QHBoxLayout *row = new QHBoxLayout;
        row->setSpacing( 8 );
        QLabel *l = new QLabel( label );
        l->setObjectName( "AdjustLabel" );
        row->addWidget( l, 1 );
        if( extra )
            row->addWidget( extra );
        ToggleSwitch *t = new ToggleSwitch;
        row->addWidget( t );
        card->addLayout( row );
        return t;
    };
    auto rule = [card]() {
        QFrame *f = new QFrame;
        f->setObjectName( "AdjustCardRule" );
        f->setFixedHeight( 1 );
        card->addWidget( f );
    };
    auto filterToggle = [this]( ToggleSwitch *t, const char *module ) {
        t->setChecked( VideoFilters::isEnabled( p_intf, module ) );
        const QByteArray mod( module );
        connect( t, &ToggleSwitch::toggled, this, [this, mod]( bool on ) {
            VideoFilters::setEnabled( p_intf, mod.constData(), on );
        } );
        registerControl( QString( "video/%1" ).arg( module ),
                         [t]() { return QVariant( t->isChecked() ); },
                         [t]( const QVariant &v ) { t->setCheckedAnimated( v.toBool() ); }, false );
    };
    auto colorOption = [this]( ColorField *f, const char *module, const char *option ) {
        f->setFixedSize( 116, 32 );
        f->setColor( QColor::fromRgb( (QRgb) VideoFilters::option( p_intf, option ).toLongLong() ) );
        const QByteArray mod( module ), opt( option );
        connect( f, &ColorField::colorChanged, this, [this, mod, opt]( const QColor &c ) {
            VideoFilters::setOption( p_intf, mod.constData(), opt.constData(),
                                     QVariant( (int) ( c.rgb() & 0xFFFFFF ) ) );
        } );
        registerControl( QString( "video/%1" ).arg( option ),
                         [f]() { return QVariant( f->color().name() ); },
                         [f]( const QVariant &v ) { f->setColor( QColor( v.toString() ) ); },
                         QVariant( f->color().name() ) );
    };

    ColorField *extract = new ColorField;
    colorOption( extract, "extract", "extract-component" );
    filterToggle( toggleRow( qtr( "Color Extraction" ), extract ), "extract" );
    rule();
    ColorField *thres = new ColorField;
    colorOption( thres, "colorthres", "colorthres-color" );
    ToggleSwitch *thresOn = toggleRow( qtr( "Color Threshold" ), thres );
    filterToggle( thresOn, "colorthres" );
    addOptionSlider( card, qtr( "Saturation" ), "colorthres", "colorthres-saturationthres", 0, 255, 20, 0, thresOn );
    addOptionSlider( card, qtr( "Similarity" ), "colorthres", "colorthres-similaritythres", 0, 255, 15, 0, thresOn );
    rule();
    filterToggle( toggleRow( qtr( "Negate Colors" ), NULL ), "invert" );
    rule();
    filterToggle( toggleRow( qtr( "Posterize" ), NULL ), "posterize" );
    rule();
    ToggleSwitch *grad = toggleRow( qtr( "Gradient" ), NULL );
    filterToggle( grad, "gradient" );
    QComboBox *gradMode = makeCombo();
    gradMode->setObjectName( "AdjustComboWide" );
    gradMode->setFixedHeight( 46 );
    gradMode->addItem( qtr( "Gradient" ), "gradient" );
    gradMode->addItem( qtr( "Edge" ), "edge" );
    gradMode->addItem( qtr( "Hough" ), "hough" );
    gradMode->setCurrentIndex( qMax( 0, gradMode->findData(
        VideoFilters::option( p_intf, "gradient-mode" ).toString() ) ) );
    connect( gradMode, QOverload<int>::of( &QComboBox::activated ), this, [this, gradMode]( int i ) {
        VideoFilters::setOption( p_intf, "gradient", "gradient-mode", gradMode->itemData( i ).toString() );
    } );
    card->addWidget( gradMode );
    ToggleSwitch *gradColor = toggleRow( qtr( "Color" ), NULL );
    gradColor->setChecked( VideoFilters::option( p_intf, "gradient-type" ).toInt() == 1 );
    connect( gradColor, &ToggleSwitch::toggled, this, [this]( bool on ) {
        VideoFilters::setOption( p_intf, "gradient", "gradient-type", QVariant( on ? 1 : 0 ) );
    } );
    ToggleSwitch *cartoon = toggleRow( qtr( "Cartoon" ), NULL );
    cartoon->setChecked( VideoFilters::option( p_intf, "gradient-cartoon" ).toBool() );
    connect( cartoon, &ToggleSwitch::toggled, this, [this]( bool on ) {
        VideoFilters::setOption( p_intf, "gradient", "gradient-cartoon", QVariant( on ) );
    } );
    auto gradEnabled = [gradMode, gradColor, cartoon]( bool on ) {
        gradMode->setEnabled( on ); gradColor->setEnabled( on ); cartoon->setEnabled( on );
    };
    gradEnabled( grad->isChecked() );
    connect( grad, &ToggleSwitch::toggled, this, gradEnabled );
    rule();
    addOptionSlider( card, qtr( "Sepia Intensity" ), "sepia", "sepia-intensity", 0, 255, 0, 2, NULL, true );

    page->addStretch( 1 );
    return area;
}

/**********************************************************************
 * Audio
 **********************************************************************/
QWidget *AdjustPanel::buildAudioTab()
{
    QVBoxLayout *page;
    QScrollArea *area = makePage( &page );

    QVBoxLayout *sec = addSection( page, qtr( "Defaults" ) );
    audioTrack = makeCombo( [this]() { fillTrackCombo( audioTrack, "audio-es" ); } );
    connect( audioTrack, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        if( THEMIM->getInput() )
            var_SetInteger( THEMIM->getInput(), "audio-es", audioTrack->itemData( i ).toLongLong() );
    } );
    addRow( sec, qtr( "Audio Track" ), audioTrack );

    QLineEdit *lang = new QLineEdit;
    lang->setObjectName( "AdjustEdit" );
    lang->setPlaceholderText( qtr( "Auto" ) );
    lang->setToolTip( qtr( "Preferred audio language codes, comma separated (e.g. en,fr)" ) );
    {
        char *psz = var_InheritString( p_intf, "audio-language" );
        lang->setText( qfu( psz ) );
        free( psz );
    }
    connect( lang, &QLineEdit::editingFinished, this, [this, lang]() {
        setSubtitleConfig( "audio-language", lang->text().trimmed() );
    } );
    addRow( sec, qtr( "Audio Language" ), lang );

    audioDevice = makeCombo( [this]() {
        QSignalBlocker b( audioDevice );
        audioDevice->clear();
        audio_output_t *aout = THEMIM->getAout();
        if( !aout )
        {
            audioDevice->addItem( qtr( "Default" ) );
            audioDevice->setEnabled( false );
            return;
        }
        char **ids, **names;
        int n = aout_DevicesList( aout, &ids, &names );
        char *cur = aout_DeviceGet( aout );
        for( int i = 0; i < n; i++ )
        {
            audioDevice->addItem( qfu( names[i] ), qfu( ids[i] ) );
            if( cur && !strcmp( cur, ids[i] ) )
                audioDevice->setCurrentIndex( i );
            free( ids[i] ); free( names[i] );
        }
        if( n > 0 ) { free( ids ); free( names ); }
        free( cur );
        vlc_object_release( aout );
        if( audioDevice->count() == 0 )
            audioDevice->addItem( qtr( "Default" ) );
        audioDevice->setEnabled( n > 1 );
    } );
    connect( audioDevice, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        audio_output_t *aout = THEMIM->getAout();
        if( !aout )
            return;
        aout_DeviceSet( aout, qtu( audioDevice->itemData( i ).toString() ) );
        vlc_object_release( aout );
    } );
    addRow( sec, qtr( "Output Device" ), audioDevice );

    stereoMode = makeCombo( [this]() {
        QSignalBlocker b( stereoMode );
        stereoMode->clear();
        vlc_object_t *aout = (vlc_object_t *) THEMIM->getAout();
        if( !aout )
        {
            stereoMode->addItem( qtr( "Default" ) );
            stereoMode->setEnabled( false );
            return;
        }
        vlc_value_t val, text;
        if( var_Change( aout, "stereo-mode", VLC_VAR_GETCHOICES, &val, &text ) == VLC_SUCCESS )
        {
            for( int i = 0; i < val.p_list->i_count; i++ )
                stereoMode->addItem( qfu( text.p_list->p_values[i].psz_string ),
                                     QVariant( (qlonglong) val.p_list->p_values[i].i_int ) );
            var_FreeList( &val, &text );
        }
        stereoMode->setCurrentIndex( stereoMode->findData( (qlonglong) var_GetInteger( aout, "stereo-mode" ) ) );
        vlc_object_release( aout );
        if( stereoMode->count() == 0 )
            stereoMode->addItem( qtr( "Default" ) );
        stereoMode->setEnabled( stereoMode->count() > 1 );
    } );
    connect( stereoMode, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        vlc_object_t *aout = (vlc_object_t *) THEMIM->getAout();
        if( !aout )
            return;
        var_SetInteger( aout, "stereo-mode", stereoMode->itemData( i ).toLongLong() );
        vlc_object_release( aout );
    } );
    addRow( sec, qtr( "Stereo Mode" ), stereoMode );

    sec = addSection( page, qtr( "Synchronization" ) );
    audioDelay = makeSpin( -600, 600, 3, 0.05, " s" );
    audioDelay->setToolTip( Theme::tooltipWithKey( p_intf,
        qtr( "A positive value means the audio is ahead of the video" ), "key-audiodelay-up" ) );
    connect( audioDelay, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double d ) {
        if( !b_refreshing && THEMIM->getInput() )
            var_SetInteger( THEMIM->getInput(), "audio-delay", (int64_t)( d * CLOCK_FREQ ) );
    } );
    addRow( sec, qtr( "Audio Track Delay" ), audioDelay );
    bindSlider( sec, audioDelay, -10, 10, 0, 2 );
    addScale( sec, QStringList() << qtr( "-10 s" ) << qtr( "0 s" ) << qtr( "+10 s" ) );

    /* Effects: VLC's own filter widgets, laid out as Figma cards */
    addAudioEffect( page, qtr( "Equalizer" ), new Equalizer( p_intf, NULL ) );
    addAudioEffect( page, qtr( "Compressor" ), new Compressor( p_intf, NULL ) );
    addAudioEffect( page, qtr( "Spatializer" ), new Spatializer( p_intf, NULL ) );
    addAudioEffect( page, qtr( "Stereo Widener" ), new StereoWidener( p_intf, NULL ) );
    addAudioEffect( page, qtr( "Pitch Shifter" ), new PitchShifter( p_intf, NULL ) );

    page->addStretch( 1 );
    return area;
}

/**********************************************************************
 * Subtitles (Figma "Subtitles" frame)
 **********************************************************************/
QWidget *AdjustPanel::buildSubtitlesTab()
{
    QVBoxLayout *page;
    QScrollArea *area = makePage( &page );

    QVBoxLayout *sec = addSection( page, qtr( "Defaults" ) );
    subTrack = makeCombo( [this]() { fillTrackCombo( subTrack, "spu-es" ); } );
    connect( subTrack, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        if( THEMIM->getInput() )
            var_SetInteger( THEMIM->getInput(), "spu-es", subTrack->itemData( i ).toLongLong() );
    } );
    addRow( sec, qtr( "Subtitle Track" ), subTrack );

    QLineEdit *lang = new QLineEdit;
    lang->setObjectName( "AdjustEdit" );
    lang->setPlaceholderText( qtr( "Auto" ) );
    lang->setToolTip( qtr( "Preferred subtitle language codes, comma separated (e.g. en,fr)" ) );
    {
        char *psz = var_InheritString( p_intf, "sub-language" );
        lang->setText( qfu( psz ) );
        free( psz );
    }
    connect( lang, &QLineEdit::editingFinished, this, [this, lang]() {
        setSubtitleConfig( "sub-language", lang->text().trimmed() );
    } );
    addRow( sec, qtr( "Subtitle Language" ), lang );

    sec = addSection( page, qtr( "Synchronization" ) );
    subDelay = makeSpin( -600, 600, 3, 0.05, " s" );
    subDelay->setToolTip( Theme::tooltipWithKey( p_intf,
        qtr( "A positive value means the subtitles are ahead of the video" ), "key-subdelay-up" ) );
    connect( subDelay, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double d ) {
        if( !b_refreshing && THEMIM->getInput() )
            var_SetInteger( THEMIM->getInput(), "spu-delay", (int64_t)( d * CLOCK_FREQ ) );
    } );
    addRow( sec, qtr( "Subtitle Track Delay" ), subDelay );
    bindSlider( sec, subDelay, -10, 10, 0 );
    addScale( sec, QStringList() << qtr( "-10 s" ) << qtr( "0 s" ) << qtr( "+10 s" ) );

    subFps = makeSpin( 1, 100, 3, 0.1, " fps" );
    subFps->setValue( 1 );
    subFps->setToolTip( qtr( "Override the frame rate of text subtitles" ) );
    connect( subFps, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double d ) {
        if( !b_refreshing && THEMIM->getInput() )
            var_SetFloat( THEMIM->getInput(), "sub-fps", d );
    } );
    addRow( sec, qtr( "Subtitle Speed" ), subFps );
    bindSlider( sec, subFps, 1, 100, 1 );

    subDuration = makeSpin( 0, 20, 3, 0.2, QString() );
    subDuration->setToolTip( qtr( "Extend subtitle duration by this factor. Set 0 to disable." ) );
    connect( subDuration, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double f ) {
        if( b_refreshing )
            return;
        config_PutFloat( p_intf, "subsdelay-factor", f );
        foreach( vout_thread_t *p_vout, THEMIM->getVouts() )
        {
            if( var_Type( p_vout, "subsdelay-factor" ) )
                var_SetFloat( p_vout, "subsdelay-factor", f );
            vlc_object_release( p_vout );
        }
        VideoFilters::setEnabled( p_intf, "subsdelay", f > 0 );
    } );
    addRow( sec, qtr( "Subtitle Duration Factor" ), subDuration );
    bindSlider( sec, subDuration, 0, 20, 0 );

    sec = addSection( page, qtr( "Appearance" ) );
    ToggleSwitch *formatted = new ToggleSwitch;
    formatted->setChecked( var_InheritBool( p_intf, "subsdec-formatted" ) );
    formatted->setToolTip( qtr( "Keep the styling embedded in the subtitles (applies to the next file)" ) );
    connect( formatted, &ToggleSwitch::toggled, this, [this]( bool on ) {
        setSubtitleConfig( "subsdec-formatted", on );
    } );
    addRow( sec, qtr( "Embedded Sub Format" ), formatted );

    QFontComboBox *font = new QFontComboBox;
    font->setObjectName( "AdjustCombo" );
    font->setFocusPolicy( Qt::StrongFocus );
    font->setMaxVisibleItems( 12 );
    {
        char *psz = var_InheritString( p_intf, "freetype-font" );
        if( psz && *psz )
            font->setCurrentFont( QFont( qfu( psz ) ) );
        free( psz );
    }
    font->setToolTip( qtr( "Applies to the next file" ) );
    font->installEventFilter( this ); /* wheel scrolls the panel unless focused */
    connect( font, &QFontComboBox::currentFontChanged, this, [this]( const QFont &f ) {
        setSubtitleConfig( "freetype-font", f.family() );
    } );
    addRow( sec, qtr( "Subtitle Font" ), font );

    struct { const char *option; QString label; } colors[] = {
        { "freetype-color", qtr( "Text Color" ) },
        { "freetype-outline-color", qtr( "Outline Color" ) },
        { "freetype-background-color", qtr( "Background Color" ) },
    };
    for( auto &c : colors )
    {
        ColorField *f = new ColorField;
        f->setColor( QColor::fromRgb( (QRgb) var_InheritInteger( p_intf, c.option ) ) );
        const char *opt = c.option;
        connect( f, &ColorField::colorChanged, this, [this, opt]( const QColor &col ) {
            setSubtitleConfig( opt, (qlonglong) ( col.rgb() & 0xFFFFFF ) );
        } );
        registerControl( QString( "sub/%1" ).arg( opt ), [f]() { return QVariant( f->color().name() ); },
                         [this, f, opt]( const QVariant &v ) {
                             f->setColor( QColor( v.toString() ) );
                             setSubtitleConfig( opt, (qlonglong) ( f->color().rgb() & 0xFFFFFF ) ); },
                         QVariant( f->color().name() ) );
        addRow( sec, c.label, f );
    }

    /* Size and outline: the module's own choice lists */
    struct { const char *option; QString label; } choices[] = {
        { "freetype-rel-fontsize", qtr( "Font Size" ) },
        { "freetype-outline-thickness", qtr( "Outline Thickness" ) },
    };
    for( auto &ch : choices )
    {
        QComboBox *c = makeCombo();
        int64_t *vals; char **texts;
        ssize_t n = config_GetIntChoices( VLC_OBJECT( p_intf ), ch.option, &vals, &texts );
        for( ssize_t i = 0; i < n; i++ )
        {
            c->addItem( qtr( texts[i] ), QVariant( (qlonglong) vals[i] ) );
            free( texts[i] );
        }
        if( n > 0 ) { free( vals ); free( texts ); }
        c->setCurrentIndex( qMax( 0, c->findData( (qlonglong) var_InheritInteger( p_intf, ch.option ) ) ) );
        const char *opt = ch.option;
        connect( c, QOverload<int>::of( &QComboBox::activated ), this, [this, c, opt]( int i ) {
            setSubtitleConfig( opt, c->itemData( i ) );
        } );
        addRow( sec, ch.label, c );
    }

    struct { const char *option; QString label; double min, max; } sliders[] = {
        { "freetype-opacity", qtr( "Text Opacity" ), 0, 255 },
        { "freetype-background-opacity", qtr( "Background Opacity" ), 0, 255 },
        { "freetype-shadow-opacity", qtr( "Shadow Opacity" ), 0, 255 },
    };
    for( auto &sl : sliders )
    {
        QDoubleSpinBox *sp = makeSpin( sl.min, sl.max, 0, 1, QString() );
        sp->setValue( var_InheritInteger( p_intf, sl.option ) );
        const char *opt = sl.option;
        module_config_t *cfg = config_FindConfig( opt );
        const int def = cfg ? (int) cfg->orig.i : 0;
        connect( sp, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this, opt]( double d ) {
            setSubtitleConfig( opt, (qlonglong) d );
        } );
        addRow( sec, sl.label, sp );
        AdjustSlider *s = bindSlider( sec, sp, sl.min, sl.max, sl.min );
        s->setResetValue( def );
        registerControl( QString( "sub/%1" ).arg( opt ), [sp]() { return QVariant( sp->value() ); },
                         [sp]( const QVariant &v ) { sp->setValue( v.toDouble() ); }, QVariant( (double) def ) );
    }

    QDoubleSpinBox *margin = makeSpin( 0, 500, 0, 1, " px" );
    margin->setValue( var_InheritInteger( p_intf, "sub-margin" ) );
    margin->setToolTip( qtr( "Distance between the subtitles and the bottom of the video" ) );
    connect( margin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double d ) {
        setVoutVar( "sub-margin", (qlonglong) d );
    } );
    addRow( sec, qtr( "Bottom Margin" ), margin );
    bindSlider( sec, margin, 0, 200, 0 );

    page->addStretch( 1 );
    return area;
}

/**********************************************************************
 * Footer: presets + advanced settings
 **********************************************************************/
QWidget *AdjustPanel::buildFooter()
{
    QWidget *f = new QWidget( this );
    f->setObjectName( "AdjustFooter" );
    f->setAttribute( Qt::WA_StyledBackground, true );
    QVBoxLayout *l = new QVBoxLayout( f );
    l->setContentsMargins( PANEL_MARGIN, 21, PANEL_MARGIN, 14 );
    l->setSpacing( 12 );

    presetButton = new QPushButton( f );
    presetButton->setObjectName( "PresetButton" );
    presetButton->setFixedHeight( 42 );
    presetButton->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    presetButton->setCursor( Qt::PointingHandCursor );
    presetButton->setMenu( new QMenu( presetButton ) );
    QHBoxLayout *pl = new QHBoxLayout( presetButton );
    pl->setContentsMargins( 16, 0, 16, 0 );
    presetLabel = new QLabel( presetButton );
    presetLabel->setObjectName( "PresetLabel" );
    presetLabel->setAttribute( Qt::WA_TransparentForMouseEvents );
    presetLabel->setText( getSettings()->value( "Adjust/preset", qtr( "Default Preset" ) ).toString() );
    pl->addWidget( presetLabel );
    pl->addStretch( 1 );
    QLabel *chev = new QLabel( presetButton );
    chev->setAttribute( Qt::WA_TransparentForMouseEvents );
    {
        QPixmap px = Theme::pixmap( "chevron", 16, Theme::colors().textMuted );
        QTransform t;
        t.rotate( -90 );
        chev->setPixmap( px.transformed( t, Qt::SmoothTransformation ) );
    }
    pl->addWidget( chev );
    connect( presetButton->menu(), &QMenu::aboutToShow, this, &AdjustPanel::rebuildPresetMenu );
    l->addWidget( presetButton );

    QToolButton *adv = new QToolButton( f );
    adv->setObjectName( "AdvancedLink" );
    adv->setText( qtr( "Open Advanced Settings" ) );
    adv->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    QIcon ic;
    ic.addPixmap( Theme::pixmap( "settings", 18, Theme::colors().textFaint ), QIcon::Normal );
    ic.addPixmap( Theme::pixmap( "settings", 18, Theme::colors().textMuted ), QIcon::Active );
    adv->setIcon( ic );
    adv->setIconSize( QSize( 18, 18 ) );
    adv->setCursor( Qt::PointingHandCursor );
    connect( adv, &QToolButton::clicked, THEDP, &DialogsProvider::prefsDialog );
    l->addWidget( adv, 0, Qt::AlignHCenter );
    return f;
}

void AdjustPanel::registerControl( const QString &key, std::function<QVariant()> get,
                                   std::function<void( const QVariant & )> set,
                                   const QVariant &def )
{
    controls << Control{ key, get, set, def };
}

void AdjustPanel::rebuildPresetMenu()
{
    QMenu *m = presetButton->menu();
    m->clear();
    m->setMinimumWidth( presetButton->width() );
    m->addAction( qtr( "Default Preset" ), this, &AdjustPanel::resetToDefaults );
    QSettings *s = getSettings();
    s->beginGroup( "AdjustPresets" );
    const QStringList names = s->childKeys();
    s->endGroup();
    foreach( const QString &n, names )
        m->addAction( n, this, [this, n]() { applyPreset( n ); } );
    m->addSeparator();
    m->addAction( qtr( "Save as Preset..." ), this, &AdjustPanel::savePresetAs );
    if( !names.isEmpty() )
    {
        QMenu *del = m->addMenu( qtr( "Delete Preset" ) );
        foreach( const QString &n, names )
            del->addAction( n, this, [this, n]() {
                getSettings()->remove( "AdjustPresets/" + n );
                if( presetLabel->text() == n )
                    presetLabel->setText( qtr( "Default Preset" ) );
            } );
    }
    m->addAction( qtr( "Keep as Default for All Media" ), this, [this]() {
        /* what the old dialog's "Save" did: persist the filter chains and
         * every value shown here */
        const char *chains[] = { "video-filter", "sub-source", "audio-filter" };
        for( const char *c : chains )
        {
            char *psz = var_GetString( THEPL, c );
            config_PutPsz( p_intf, c, psz );
            free( psz );
        }
        const char *opts[] = { "hue", "brightness", "contrast", "saturation",
                               "gamma", "sharpen-sigma", "gradfun-radius", "grain-variance",
                               "extract-component", "colorthres-color", "colorthres-saturationthres",
                               "colorthres-similaritythres", "gradient-mode", "gradient-type",
                               "gradient-cartoon", "sepia-intensity", "transform-type",
                               "equalizer-bands", "equalizer-preamp", "equalizer-2pass",
                               "equalizer-preset", "rate" };
        for( const char *o : opts )
        {
            QVariant v = VideoFilters::option( p_intf, o );
            vlc_object_t *aout = (vlc_object_t *) THEMIM->getAout();
            if( aout && var_Type( aout, o ) )
            {
                vlc_value_t val;
                const int t = var_Type( aout, o ) & VLC_VAR_CLASS;
                if( !var_GetChecked( aout, o, t, &val ) )
                {
                    if( t == VLC_VAR_FLOAT ) v = val.f_float;
                    else if( t == VLC_VAR_STRING ) { v = qfu( val.psz_string ); free( val.psz_string ); }
                    else if( t == VLC_VAR_BOOL ) v = (bool) val.b_bool;
                    else v = (qlonglong) val.i_int;
                }
            }
            if( aout )
                vlc_object_release( aout );
            if( !v.isValid() )
                continue;
            switch( config_GetType( o ) & VLC_VAR_CLASS )
            {
            case VLC_VAR_STRING: config_PutPsz( p_intf, o, qtu( v.toString() ) ); break;
            case VLC_VAR_FLOAT: config_PutFloat( p_intf, o, v.toFloat() ); break;
            case VLC_VAR_BOOL:
            case VLC_VAR_INTEGER: config_PutInt( p_intf, o, v.toLongLong() ); break;
            }
        }
        config_SaveConfigFile( p_intf );
    } );
}

void AdjustPanel::applyPreset( const QString &name )
{
    const QVariantMap map = getSettings()->value( "AdjustPresets/" + name ).toMap();
    foreach( const Control &c, controls )
        if( map.contains( c.key ) )
            c.set( map.value( c.key ) );
    presetLabel->setText( name );
    getSettings()->setValue( "Adjust/preset", name );
}

void AdjustPanel::savePresetAs()
{
    bool ok;
    QString name = QInputDialog::getText( window(), qtr( "Save Preset" ), qtr( "Preset name:" ),
                                          QLineEdit::Normal, QString(), &ok ).trimmed();
    if( !ok || name.isEmpty() )
        return;
    name.replace( '/', '-' );
    QVariantMap map;
    foreach( const Control &c, controls )
        map.insert( c.key, c.get() );
    getSettings()->setValue( "AdjustPresets/" + name, map );
    presetLabel->setText( name );
    getSettings()->setValue( "Adjust/preset", name );
}

void AdjustPanel::resetToDefaults()
{
    foreach( const Control &c, controls )
        c.set( c.def );
    presetLabel->setText( qtr( "Default Preset" ) );
    getSettings()->setValue( "Adjust/preset", qtr( "Default Preset" ) );
}

/**********************************************************************
 * Live state
 **********************************************************************/
void AdjustPanel::refreshInput()
{
    b_refreshing = true;
    input_thread_t *p_input = THEMIM->getInput();
    if( videoTrack ) fillTrackCombo( videoTrack, "video-es" );
    if( audioTrack ) fillTrackCombo( audioTrack, "audio-es" );
    if( subTrack )   fillTrackCombo( subTrack, "spu-es" );
    QComboBox *aoutCombos[] = { audioDevice, stereoMode };
    for( QComboBox *c : aoutCombos )
        if( PanelCombo *pc = static_cast<PanelCombo *>( c ) )
            if( pc->aboutToShow )
                pc->aboutToShow();
    QDoubleSpinBox *perInput[] = { audioDelay, subDelay, subFps, subDuration };
    for( QDoubleSpinBox *s : perInput )
        if( s ) s->setEnabled( p_input != NULL );
    if( p_input )
    {
        if( audioDelay ) audioDelay->setValue( var_GetInteger( p_input, "audio-delay" ) / (double) CLOCK_FREQ );
        if( subDelay )   subDelay->setValue( var_GetInteger( p_input, "spu-delay" ) / (double) CLOCK_FREQ );
        if( subFps )     subFps->setValue( qMax( 1.f, var_GetFloat( p_input, "sub-fps" ) ) );
        /* the factor only means something while the filter runs */
        if( subDuration ) subDuration->setValue( VideoFilters::isEnabled( p_intf, "subsdelay" )
                                                 ? var_InheritFloat( p_intf, "subsdelay-factor" ) : 0. );
        if( rateSpin )   rateSpin->setValue( var_GetFloat( p_input, "rate" ) );
    }
    /* keep the sliders in step with their boxes */
    for( QDoubleSpinBox *s : perInput )
        if( s )
            if( AdjustSlider *sl = s->property( "slider" ).value<AdjustSlider *>() )
            {
                sl->setEnabled( p_input != NULL );
            }
    if( !p_input && abButton )
        abButton->setText( qtr( "Set A" ) );
    if( abButton )
        abButton->setEnabled( p_input != NULL );
    b_refreshing = false;
}

/* The effect widgets read their values when built; the audio output (and
 * with it a preset's band values) may only exist later. */
void AdjustPanel::refreshAudioEffects()
{
    for( auto &e : audioEffects )
    {
        const bool on = e.second->isFilterEnabled();
        {
            QSignalBlocker b( e.first );
            e.first->setChecked( on );
        }
        e.second->setEnabled( on );
        e.second->reload();
    }
}

void AdjustPanel::refreshVout()
{
    foreach( const std::function<void()> &f, voutAppliers )
        f();
}

/**********************************************************************
 * AdjustDock
 **********************************************************************/
AdjustDock::AdjustDock( intf_thread_t *p_intf, QWidget *parent )
    : QWidget( parent ), open( false )
{
    setObjectName( "VLCAdjustDock" );
    setFixedWidth( 0 );
    p = new AdjustPanel( p_intf, this );
    anim = new QVariantAnimation( this );
    anim->setDuration( 200 );
    anim->setEasingCurve( QEasingCurve::OutCubic );
    connect( anim, &QVariantAnimation::valueChanged, this, [this]( const QVariant &v ) {
        setFixedWidth( v.toInt() );
    } );
    connect( anim, &QVariantAnimation::finished, this, [this]() {
        if( !open )
            hide();
    } );
    hide();
}

void AdjustDock::setOpen( bool b, bool animate )
{
    const int full = AdjustPanel::PANEL_WIDTH + GAP;
    open = b;
    anim->stop();
    if( b )
        show();
    if( !animate || !window()->isVisible() )
    {
        setFixedWidth( b ? full : 0 );
        setVisible( b );
    }
    else
    {
        anim->setStartValue( width() );
        anim->setEndValue( b ? full : 0 );
        anim->start();
    }
    if( b )
        p->setFocus( Qt::OtherFocusReason );
}

void AdjustDock::resizeEvent( QResizeEvent *e )
{
    QWidget::resizeEvent( e );
    p->setGeometry( width() - AdjustPanel::PANEL_WIDTH, 0, AdjustPanel::PANEL_WIDTH, height() );
}
