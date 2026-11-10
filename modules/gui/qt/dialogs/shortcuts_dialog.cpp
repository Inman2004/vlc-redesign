/*****************************************************************************
 * shortcuts_dialog.cpp : the redesigned Keyboard Shortcuts dialog (P6b)
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

/*
 * Measured on the canvas frame (row 3, x 7340), scaled x1.4327, relative
 * to the 1048 x 800 frame:
 *   title row 50; search 10..828 x 53..87, "All Fields" 839..1038; the
 *   dark header ends at 96
 *   column titles centred at y 129 (Action x 20, Hotkey and Global Hotkey
 *   centred over their columns); rule at 160
 *   section title centred 35px under a rule; first row 50px under it;
 *   rows 32px on a 47px pitch; hotkey cells 399..668, global 757..1027
 *   cell: #090909, r4, key centred, 16px pencil 17px from the right
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/shortcuts_dialog.hpp"
#include "styles/vlc_theme.hpp"
#include "components/settings_controls.hpp"
#include "util/customwidgets.hpp"
#include "util/key_map.hpp"

#include <vlc_actions.h>
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

enum
{
    COL_HOTKEY_X = 399, COL_GLOBAL_X = 757, CELL_W = 270, CELL_H = 32,
    ROW_PITCH = 47, SIDE = 20,
};

QFont uiFont( int px )
{
    QFont f( "Google Sans" );
    f.setPixelSize( px );
    return f;
}

/* the design's sections; keys not listed here go to "Other" */
struct Group { const char *title; const char *const *keys; };
const char *const basicKeys[] = {
    "toggle-fullscreen", "leave-fullscreen", "position", "intf-show", "intf-boss",
    "intf-popup-menu", "quit", NULL };
const char *const playbackKeys[] = {
    "play-pause", "stop", "next", "prev", "program-sid-next", "program-sid-prev",
    "play", "pause", "random", "loop", "clear-playlist", "frame-next",
    "faster", "slower", "rate-normal", "rate-faster-fine", "rate-slower-fine", NULL };
const char *const seekKeys[] = {
    "jump+extrashort", "jump-extrashort", "jump+short", "jump-short", "jump+medium",
    "jump-medium", "jump+long", "jump-long", "title-prev", "title-next",
    "chapter-prev", "chapter-next", NULL };
const char *const audioKeys[] = {
    "vol-up", "vol-down", "vol-mute", "audio-track", "audiodevice-cycle",
    "audiodelay-up", "audiodelay-down", NULL };
const char *const subtitleKeys[] = {
    "subtitle-track", "subtitle-revtrack", "subtitle-toggle", "subdelay-up", "subdelay-down",
    "subsync-markaudio", "subsync-marksub", "subsync-apply", "subsync-reset",
    "subpos-up", "subpos-down", "subtitle-text-scale-normal", "subtitle-text-scale-up",
    "subtitle-text-scale-down", NULL };
const char *const videoKeys[] = {
    "aspect-ratio", "crop", "toggle-autoscale", "incr-scalefactor", "decr-scalefactor",
    "deinterlace", "deinterlace-mode", "zoom", "unzoom", "zoom-quarter", "zoom-half",
    "zoom-original", "zoom-double", "snapshot", "record", "wallpaper",
    "crop-top", "uncrop-top", "crop-left", "uncrop-left", "crop-bottom", "uncrop-bottom",
    "crop-right", "uncrop-right", "viewpoint-fov-in", "viewpoint-fov-out",
    "viewpoint-roll-clock", "viewpoint-roll-anticlock", NULL };
const char *const discKeys[] = {
    "disc-menu", "nav-activate", "nav-up", "nav-down", "nav-left", "nav-right", NULL };
const char *const bookmarkKeys[] = {
    "set-bookmark1", "set-bookmark2", "set-bookmark3", "set-bookmark4", "set-bookmark5",
    "set-bookmark6", "set-bookmark7", "set-bookmark8", "set-bookmark9", "set-bookmark10",
    "play-bookmark1", "play-bookmark2", "play-bookmark3", "play-bookmark4", "play-bookmark5",
    "play-bookmark6", "play-bookmark7", "play-bookmark8", "play-bookmark9", "play-bookmark10",
    NULL };
const Group groups[] = {
    { N_("Basic"), basicKeys },
    { N_("Playback"), playbackKeys },
    { N_("Seeking"), seekKeys },
    { N_("Audio"), audioKeys },
    { N_("Subtitles"), subtitleKeys },
    { N_("Video"), videoKeys },
    { N_("Disc Menus"), discKeys },
    { N_("Bookmarks"), bookmarkKeys },
};

/* the design's names for the rows it draws; the rest use VLC's own */
QString designLabel( const QString &key )
{
    static const char *const names[][2] = {
        { "toggle-fullscreen", N_("Toggle fullscreen") },
        { "leave-fullscreen", N_("Exit fullscreen / close dialog") },
        { "position", N_("Show playback position and time") },
        { "intf-show", N_("Show interface controls in fullscreen") },
        { "play-pause", N_("Play / pause") },
        { "stop", N_("Stop playback") },
        { "next", N_("Next track") },
        { "prev", N_("Previous track") },
        { "program-sid-next", N_("Next program / Service ID") },
        { "program-sid-prev", N_("Previous program / Service ID") },
    };
    for( const auto &n : names )
        if( key == n[0] )
            return qtr( n[1] );
    return QString();
}

} /* namespace */

/*********************************************************************
 * KeyBox
 *********************************************************************/
KeyBox::KeyBox( const QString &o, QWidget *parent )
    : QWidget( parent ), opt( o ), capturing( false ), hovered( false ), available( true )
{
    setFixedSize( CELL_W, CELL_H );
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::ClickFocus );
    setAttribute( Qt::WA_Hover );
    setToolTip( qtr( "Click, then press the new key. Backspace clears it, Esc cancels." ) );
}

QString KeyBox::pretty( const QString &stored )
{
    QStringList out;
    for( const QString &k : stored.split( '\t', Qt::SkipEmptyParts ) )
    {
        const uint32_t code = vlc_str2keycode( k.toUtf8().constData() );
        QString s = code == KEY_UNSET ? k : VLCKeyToString( code, true );
        /* "Ctrl+q" -> "Ctrl+Q", as drawn */
        const int plus = s.lastIndexOf( '+', s.length() - 2 );
        QString last = s.mid( plus + 1 );
        if( last.length() == 1 )
            s = s.left( plus + 1 ) + last.toUpper();
        out << s;
    }
    return out.join( ", " );
}

QString KeyBox::display() const { return pretty( val ); }

void KeyBox::setValue( const QString &v )
{
    val = v;
    update();
}

void KeyBox::setConflict( const QString &other )
{
    if( other == conflict )
        return;
    conflict = other;
    if( available )
        setToolTip( conflict.isEmpty()
            ? qtr( "Click, then press the new key. Backspace clears it, Esc cancels." )
            : qtr( "Also used by \"%1\"." ).arg( conflict ) );
    update();
}

void KeyBox::setAvailable( bool a, const QString &reason )
{
    available = a;
    setEnabled( a );
    if( !a )
        setToolTip( reason );
}

void KeyBox::enterEvent( QEvent * ) { hovered = true; update(); }
void KeyBox::leaveEvent( QEvent * ) { hovered = false; update(); }

void KeyBox::mousePressEvent( QMouseEvent * )
{
    if( !available )
        return;
    capturing = true;
    setFocus( Qt::MouseFocusReason );
    grabKeyboard();
    update();
}

void KeyBox::focusOutEvent( QFocusEvent *e )
{
    if( capturing )
    {
        capturing = false;
        releaseKeyboard();
        update();
    }
    QWidget::focusOutEvent( e );
}

bool KeyBox::event( QEvent *e )
{
    /* while capturing, every key (Tab and Esc included) is ours */
    if( capturing && ( e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride ) )
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>( e );
        e->accept();
        if( e->type() == QEvent::ShortcutOverride )
            return true;
        const int k = ke->key();
        if( k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt
         || k == Qt::Key_Meta || k == Qt::Key_AltGr || k == Qt::Key_Super_L
         || k == Qt::Key_Super_R || k == Qt::Key_unknown )
            return true;   /* wait for the real key */
        capturing = false;
        releaseKeyboard();
        if( k == Qt::Key_Escape && ke->modifiers() == Qt::NoModifier )
        {
            update();
            return true;
        }
        QString v;
        if( !( ( k == Qt::Key_Backspace || k == Qt::Key_Delete ) && ke->modifiers() == Qt::NoModifier ) )
        {
            const int code = qtEventToVLCKey( ke );
            char *psz = vlc_keycode2str( code, false );
            if( psz )
            {
                v = QString::fromUtf8( psz );
                free( psz );
            }
        }
        update();
        emit captured( this, v );
        return true;
    }
    return QWidget::event( e );
}

void KeyBox::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    const QRectF r = QRectF( rect() ).adjusted( .5, .5, -.5, -.5 );
    QColor bg( Theme::isDark() ? "#090909" : "#F2F1EF" );
    if( hovered && available && !capturing )
        bg = bg.lighter( 160 );
    p.setBrush( bg );
    if( capturing )
        p.setPen( QPen( Theme::accent(), 1 ) );
    else if( !conflict.isEmpty() )
        p.setPen( QPen( QColor( "#C0503A" ), 1 ) );
    else
        p.setPen( Qt::NoPen );
    p.drawRoundedRect( r, 4, 4 );

    const QColor text = Theme::isDark() ? QColor( "#D6D6D6" ) : QColor( "#1E1E1E" );
    const QColor muted( "#6B6B6B" );
    p.setFont( uiFont( 13 ) );
    QRect textR = rect().adjusted( 12, 0, -40, 0 );
    /* the design centres the key over the whole cell */
    QRect centred = rect().adjusted( 30, 0, -30, 0 );
    if( capturing )
    {
        p.setPen( Theme::accent() );
        p.drawText( centred, Qt::AlignCenter, qtr( "Press a key…" ) );
    }
    else
    {
        const QString s = display();
        p.setPen( available ? text : muted );
        const int w = p.fontMetrics().horizontalAdvance( s );
        if( w <= centred.width() )
            p.drawText( centred, Qt::AlignCenter, s );
        else
            p.drawText( textR, Qt::AlignVCenter | Qt::AlignLeft,
                        p.fontMetrics().elidedText( s, Qt::ElideRight, textR.width() ) );
    }
    const int g = 16;
    QColor pc( hovered && available ? "#C6C6C6" : "#8F8F8F" );
    if( !available ) pc = QColor( "#404040" );
    p.drawPixmap( width() - 17 - g / 2, ( height() - g ) / 2,
                  Theme::pixmap( "edit", g, capturing ? Theme::accent() : pc ) );
}

/*********************************************************************
 * ShortcutsDialog
 *********************************************************************/
ShortcutsDialog::ShortcutsDialog( intf_thread_t *_p_intf, QWidget *parent )
    : RedesignDialog( _p_intf, qtr( "Keyboard Shortcuts" ), parent )
{
    setDesignSize( 1048, 800 );
    primaryButton()->setText( qtr( "Save" ) );
    primaryButton()->setEnabled( false );

    /* search + field filter in the dark header: 3px under the title row,
     * 34px tall, 9px above the body */
    QWidget *bar = new QWidget( header() );
    QHBoxLayout *bl = new QHBoxLayout( bar );
    bl->setContentsMargins( 10, 3, 10, 9 );
    bl->setSpacing( 11 );
    search = new QLineEdit( bar );
    search->setObjectName( "RDToolbarField" );
    search->setFixedHeight( 34 );
    search->setPlaceholderText( qtr( "Search Keyboard Shortcuts..." ) );
    search->addAction( QIcon( Theme::pixmap( "search", 18, QColor( "#8F8F8F" ) ) ),
                       QLineEdit::LeadingPosition );
    field = new SCombo( bar );
    field->setFixedSize( 199, 34 );
    field->addItem( qtr( "All Fields" ) );
    field->addItem( qtr( "Action" ) );
    field->addItem( qtr( "Hotkey" ) );
    field->addItem( qtr( "Global Hotkey" ) );
    bl->addWidget( search, 1 );
    bl->addWidget( field );
    static_cast<QVBoxLayout *>( header()->layout() )->addWidget( bar );
    connect( search, &QLineEdit::textChanged, this, &ShortcutsDialog::filter );
    connect( field, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &ShortcutsDialog::filter );

    build();
    updateConflicts();
}

void ShortcutsDialog::build()
{
    body()->setContentsMargins( 0, 0, 0, 0 );
    const QString headCss = "color:#6B6B6B; background:transparent;";
    const QString sectionCss = "color:#8F8F8F; background:transparent;";
    const QString labelCss = Theme::isDark() ? "color:#C6C6C6; background:transparent;"
                                              : "color:#1E1E1E; background:transparent;";
    const bool globalOk = module_exists( "globalhotkeys" );

    /* column titles stay put; the sections scroll under them */
    QWidget *cols = new QWidget;
    cols->setFixedHeight( 64 );
    {
        QLabel *a = new QLabel( qtr( "Action" ), cols );
        QLabel *h = new QLabel( qtr( "Hotkey" ), cols );
        QLabel *g = new QLabel( qtr( "Global Hotkey" ), cols );
        for( QLabel *l : { a, h, g } )
        {
            l->setFont( uiFont( 14 ) );
            l->setStyleSheet( headCss );
            l->setAlignment( Qt::AlignCenter );
        }
        a->setAlignment( Qt::AlignVCenter | Qt::AlignLeft );
        a->setGeometry( SIDE, 22, 300, 22 );
        h->setGeometry( COL_HOTKEY_X, 22, CELL_W, 22 );
        g->setGeometry( COL_GLOBAL_X, 22, CELL_W, 22 );
        g->setToolTip( globalOk
            ? qtr( "Global hotkeys also work while another application has the focus. "
                   "Changes to them apply the next time VLC starts." )
            : qtr( "Global hotkeys need the globalhotkeys module, which this build does not have." ) );
        QFrame *rule = new QFrame( cols );
        rule->setObjectName( "RDRule" );
        rule->setGeometry( SIDE, 63, 1048 - 2 * SIDE, 1 );
    }
    body()->addWidget( cols );

    QScrollArea *sa = new QScrollArea;
    sa->setObjectName( "RDRows" );
    sa->setFrameShape( QFrame::NoFrame );
    sa->setWidgetResizable( true );
    sa->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    QWidget *host = new QWidget;
    host->setObjectName( "RDRowsHost" );
    host->setStyleSheet( "QWidget#RDRowsHost { background: transparent; }" );
    sa->setStyleSheet( "QScrollArea#RDRows { background: transparent; }"
                       "QScrollArea#RDRows > QWidget > QWidget { background: transparent; }" );
    QVBoxLayout *v = new QVBoxLayout( host );
    v->setContentsMargins( 0, 0, 0, 12 );
    v->setSpacing( 0 );
    sa->setWidget( host );
    body()->addWidget( sa, 1 );

    /* every key-* option of the core, sorted into the design's sections */
    unsigned n;
    module_config_t *cfg = module_config_get( module_get_main(), &n );
    QHash<QString, module_config_t *> keys;
    QStringList order;
    for( unsigned i = 0; i < n; i++ )
        if( cfg[i].i_type == CONFIG_ITEM_KEY && cfg[i].psz_name
         && !strncmp( cfg[i].psz_name, "key-", 4 ) )
        {
            keys.insert( QString( cfg[i].psz_name + 4 ), &cfg[i] );
            order << QString( cfg[i].psz_name + 4 );
        }

    QList<QPair<QString, QStringList> > layout;
    QSet<QString> placed;
    for( const Group &g : groups )
    {
        QStringList l;
        for( const char *const *k = g.keys; *k; k++ )
            if( keys.contains( *k ) ) { l << *k; placed.insert( *k ); }
        layout << qMakePair( qtr( g.title ), l );
    }
    QStringList rest;
    for( const QString &k : order )
        if( !placed.contains( k ) ) rest << k;
    if( !rest.isEmpty() )
        layout << qMakePair( qtr( "Other" ), rest );

    for( int si = 0; si < layout.count(); si++ )
    {
        if( layout[si].second.isEmpty() )
            continue;
        Section sec;
        /* rule (not above the first), then the title 35px under it */
        QWidget *head = new QWidget( host );
        const bool first = sections.isEmpty();
        /* first: title centred 35px under the column rule, cells 69px
         * under it; later: rule 25px under the last cell (whose row
         * already carries 15px), title 35px under the rule, cells 34px
         * under the title's centre */
        head->setFixedHeight( first ? 69 : 79 );
        sec.rule = NULL;
        if( !first )
        {
            QFrame *rule = new QFrame( head );
            rule->setObjectName( "RDRule" );
            rule->setGeometry( SIDE, 10, 1048 - 2 * SIDE, 1 );
            sec.rule = rule;
        }
        QLabel *t = new QLabel( layout[si].first, head );
        t->setFont( uiFont( 15 ) );
        t->setStyleSheet( sectionCss );
        t->setGeometry( SIDE, first ? 24 : 34, 400, 22 );
        v->addWidget( head );
        sec.heading = head;
        sec.title = t;

        for( const QString &k : layout[si].second )
        {
            module_config_t *c = keys.value( k );
            QString label = designLabel( k );
            if( label.isEmpty() )
                label = c->psz_text ? qtr( c->psz_text ) : k;
            QWidget *rw = new QWidget( host );
            rw->setFixedHeight( ROW_PITCH );
            QLabel *l = new QLabel( label, rw );
            l->setFont( uiFont( 14 ) );
            l->setStyleSheet( labelCss );
            l->setGeometry( SIDE, 0, COL_HOTKEY_X - SIDE - 16, CELL_H );
            l->setToolTip( QString( "--key-%1" ).arg( k ) );
            KeyBox *hk = new KeyBox( QString( "key-" ) + k, rw );
            hk->move( COL_HOTKEY_X, 0 );
            KeyBox *gk = new KeyBox( QString( "global-key-" ) + k, rw );
            gk->move( COL_GLOBAL_X, 0 );
            if( !globalOk )
                gk->setAvailable( false, qtr( "Global hotkeys need the globalhotkeys module, "
                                              "which this build does not have." ) );
            for( KeyBox *b : { hk, gk } )
            {
                char *psz = config_GetPsz( p_intf, qtu( b->option() ) );
                const QString val = psz ? qfu( psz ) : QString();
                free( psz );
                b->setValue( val );
                original.insert( b->option(), val );
                rowOf.insert( b, rows.count() );
                connect( b, &KeyBox::captured, this, &ShortcutsDialog::onCaptured );
            }
            v->addWidget( rw );
            sec.rows << rows.count();
            rows << Row{ rw, label, hk, gk, sections.count() };
        }
        sections << sec;
    }
    module_config_free( cfg );
    v->addStretch( 1 );
}

void ShortcutsDialog::onCaptured( KeyBox *box, const QString &v )
{
    const bool global = box->option().startsWith( "global-" );
    if( !v.isEmpty() )
    {
        /* the same key on another action of this column? */
        for( const Row &r : rows )
        {
            KeyBox *other = global ? r.global : r.hotkey;
            if( other == box )
                continue;
            QStringList ks = other->value().split( '\t', Qt::SkipEmptyParts );
            if( !ks.contains( v ) )
                continue;
            const int ans = QMessageBox::question( this, qtr( "Keyboard Shortcuts" ),
                qtr( "%1 is already used by \"%2\". Use it for \"%3\" instead?" )
                    .arg( KeyBox::pretty( v ), r.label, rows[rowOf.value( box )].label ) );
            if( ans != QMessageBox::Yes )
                return;
            ks.removeAll( v );
            other->setValue( ks.join( '\t' ) );
        }
    }
    box->setValue( v );
    updateConflicts();
    updateDirty();
}

void ShortcutsDialog::updateConflicts()
{
    for( int col = 0; col < 2; col++ )
    {
        QHash<QString, QList<int> > users;
        for( int i = 0; i < rows.count(); i++ )
        {
            KeyBox *b = col ? rows[i].global : rows[i].hotkey;
            for( const QString &k : b->value().split( '\t', Qt::SkipEmptyParts ) )
                users[k] << i;
        }
        for( int i = 0; i < rows.count(); i++ )
        {
            KeyBox *b = col ? rows[i].global : rows[i].hotkey;
            QString other;
            for( const QString &k : b->value().split( '\t', Qt::SkipEmptyParts ) )
                for( int j : users.value( k ) )
                    if( j != i ) other = rows[j].label;
            b->setConflict( other );
        }
    }
}

void ShortcutsDialog::updateDirty()
{
    bool dirty = false;
    for( const Row &r : rows )
        for( KeyBox *b : { r.hotkey, r.global } )
            dirty = dirty || b->value() != original.value( b->option() );
    primaryButton()->setEnabled( dirty );
}

void ShortcutsDialog::filter()
{
    const QString q = search->text().trimmed().toLower();
    const int f = field->currentIndex();   /* 0 all, 1 action, 2 hotkey, 3 global */
    QVector<int> shown( sections.count(), 0 );
    for( const Row &r : rows )
    {
        bool m = q.isEmpty();
        if( !m && ( f == 0 || f == 1 ) ) m = r.label.toLower().contains( q );
        if( !m && ( f == 0 || f == 2 ) ) m = r.hotkey->display().toLower().contains( q );
        if( !m && ( f == 0 || f == 3 ) ) m = r.global->display().toLower().contains( q );
        r.widget->setVisible( m );
        shown[r.section] += m;
    }
    /* the first section still showing gets the top spacing, no rule */
    bool first = true;
    for( int i = 0; i < sections.count(); i++ )
    {
        Section &sec = sections[i];
        sec.heading->setVisible( shown[i] > 0 );
        if( shown[i] == 0 )
            continue;
        sec.heading->setFixedHeight( first ? 69 : 79 );
        sec.title->move( SIDE, first ? 24 : 34 );
        if( sec.rule )
            sec.rule->setVisible( !first );
        first = false;
    }
}

void ShortcutsDialog::accept()
{
    bool changed = false;
    for( const Row &r : rows )
        for( KeyBox *b : { r.hotkey, r.global } )
            if( b->value() != original.value( b->option() ) )
            {
                config_PutPsz( p_intf, qtu( b->option() ), qtu( b->value() ) );
                changed = true;
            }
    if( changed )
    {
        config_SaveConfigFile( p_intf );
        KeyMap::reload( p_intf );
    }
    RedesignDialog::accept();
}
