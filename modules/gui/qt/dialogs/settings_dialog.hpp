/*****************************************************************************
 * settings_dialog.hpp : the redesigned Settings window (P6)
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

#ifndef VLC_QT_SETTINGS_DIALOG_HPP_
#define VLC_QT_SETTINGS_DIALOG_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "util/singleton.hpp"

#include <QHash>
#include <QList>
#include <QPair>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class SCombo;
class SField;
class SToggle;
class STickSlider;
class SColorField;
class SettingsNav;
class SettingsPage;
class SettingsRow;

/* Figma row 4 "Settings - *": a 1019 x 841 window, search and a grouped
 * category list on the left, a titled page of sections on the right.
 * Every row is bound to a VLC option, to a setting the Qt interface owns,
 * or shown disabled with the reason (see the vault's "P6 Settings" audit).
 * Changes apply at once; the configuration is saved when it closes. */
class SettingsDialog : public QWidget, public Singleton<SettingsDialog>
{
    Q_OBJECT
    friend class Singleton<SettingsDialog>;
public:
    enum Page
    {
        GENERAL, INTERFACE, PRIVACY, CONTROLS,
        PLAYBACK, VIDEO, AUDIO, SUBTITLES, LIBRARY,
        NETWORK, PERFORMANCE, RECORDING, EXTENSIONS,
        PAGE_COUNT
    };
    void showPage( int page );
    /* re-read every row from the configuration */
    void refreshAll();

protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
    void showEvent( QShowEvent * ) Q_DECL_OVERRIDE;
    void hideEvent( QHideEvent * ) Q_DECL_OVERRIDE;
    void keyPressEvent( QKeyEvent * ) Q_DECL_OVERRIDE;
    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;

private:
    SettingsDialog( intf_thread_t * );
    virtual ~SettingsDialog();

    /* pages */
    void buildGeneral();
    void buildInterface();
    void buildPrivacy();
    void buildControls();
    void buildPlayback();
    void buildVideo();
    void buildAudio();
    void buildSubtitles();
    void buildLibrary();
    void buildNetwork();
    void buildPerformance();
    void buildRecording();
    void buildExtensions();

    /* row builders bound to VLC options */
    SettingsRow *row( SettingsPage *, const QString &label, const QString &tip,
                      QWidget *control, const char *option = NULL );
    SToggle *boolOption( SettingsPage *, const QString &label, const QString &tip,
                         const char *option, bool invert = false );
    SCombo *intChoice( SettingsPage *, const QString &label, const QString &tip,
                       const char *option,
                       const QList<QPair<QString, int> > &items = QList<QPair<QString, int> >() );
    SCombo *stringChoice( SettingsPage *, const QString &label, const QString &tip,
                          const char *option,
                          const QList<QPair<QString, QString> > &items = QList<QPair<QString, QString> >() );
    SField *textOption( SettingsPage *, const QString &label, const QString &tip,
                        const char *option, const QString &placeholder = QString(),
                        bool password = false );
    SField *numberOption( SettingsPage *, const QString &label, const QString &tip,
                          const char *option, const QString &suffix = QString(),
                          double scale = 1.0 );
    SField *pathOption( SettingsPage *, const QString &label, const QString &tip,
                        const char *option, bool directory, const QString &placeholder = QString() );
    STickSlider *sliderOption( SettingsPage *, const QString &label, const QString &tip,
                               const char *option, double min, double max, double step,
                               int ticks, const QVector<QPair<double, QString> > &labels,
                               double scale = 1.0 );
    SColorField *colorOption( SettingsPage *, const QString &label, const QString &tip,
                              const char *option );
    SToggle *filterToggle( SettingsPage *, const QString &label, const QString &tip,
                           const char *chainOption, const char *module );
    /* a setting the Qt interface keeps in its own QSettings */
    SToggle *qtToggle( SettingsPage *, const QString &label, const QString &tip,
                       const QString &key, bool def, std::function<void( bool )> apply = nullptr );
    /* a row the design draws but VLC 3 cannot do: disabled, with the reason */
    SettingsRow *unavailable( SettingsPage *, const QString &label, const QString &reason,
                              QWidget *control );

    void onRefresh( const char *option, std::function<void()> f );
    void changed( const char *option );
    void setFilterModule( const char *chain, const char *module, bool on );
    bool hasFilterModule( const char *chain, const char *module );

    void applySearch( const QString & );
    void layoutChrome();
    void restyle();
    void updateDeveloperTips();

    intf_thread_t *p_intf;
    QWidget *frame;
    QLineEdit *search;
    QLabel *title;
    QToolButton *closeButton;
    SettingsNav *nav;
    QStackedWidget *stack;
    QLabel *noResults;
    QList<SettingsPage *> pages;
    QList<SettingsRow *> rows;
    QMultiHash<QString, std::function<void()> > refreshers;
    bool dirty;
    bool themeDirty;
    bool building;
};

#endif
