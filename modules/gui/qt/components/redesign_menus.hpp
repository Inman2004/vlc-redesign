/*****************************************************************************
 * redesign_menus.hpp : main menu and track popover of the redesign (P4b)
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

#ifndef VLC_QT_REDESIGN_MENUS_HPP_
#define VLC_QT_REDESIGN_MENUS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>
#include <QIcon>
#include <QPointer>
#include <functional>

class QMenu;
class QVBoxLayout;
class QStackedWidget;
class QButtonGroup;
class QAbstractButton;
class QVariantAnimation;

namespace RedesignMenus
{
    /* 20px glyph in the accent, as every Figma menu draws them */
    QIcon menuIcon( const QString &name, int size = 24 );
    /* "Label\tCtrl + O": the shortcut column without a second shortcut */
    QString withShortcut( const QString &label, const QString &keys );
    /* the header's single main menu (Components column) */
    QMenu *mainMenu( intf_thread_t *, QWidget *parent );
    /* profile names (P8b stores what a profile means) */
    QStringList profiles( intf_thread_t * );
    QString currentProfile( intf_thread_t * );
}

/* Video / Audio / Subtitles popover above the bar's track buttons
 * (Figma Components: 400x522, #0E0E0E, 50px tab strip) */
class TrackPopover : public QFrame
{
    Q_OBJECT
public:
    enum Tab { VIDEO_TAB, AUDIO_TAB, SUBTITLES_TAB };
    /* open on a tab above anchor, or close it when it is already open */
    static void toggle( intf_thread_t *, QAbstractButton *anchor, int tab );

protected:
    void hideEvent( QHideEvent * ) override;
    void keyPressEvent( QKeyEvent * ) override;

private:
    TrackPopover( intf_thread_t *, QAbstractButton *anchor );
    void setTab( int, bool animate );
    void rebuild();
    void moveUnderline( bool animate );
    QWidget *sectionHeader( const QString &title, QWidget *extra = NULL );
    QWidget *row( const QString &text, bool checked, std::function<void()> onClick );
    void addRule( QVBoxLayout * );
    void fillEs( QVBoxLayout *, const char *var, int skipLast = 0 );

    intf_thread_t *p_intf;
    QPointer<QAbstractButton> anchor;
    QIcon anchorIcon;
    int tab;
    QButtonGroup *tabs;
    QWidget *tabBar, *underline;
    QVariantAnimation *underlineAnim;
    QWidget *pageHost;
    QVBoxLayout *pageLayout;
};

#endif
