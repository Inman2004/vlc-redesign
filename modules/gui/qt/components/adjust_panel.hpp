/*****************************************************************************
 * adjust_panel.hpp : redesigned adjustments sidebar (P3)
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

#ifndef VLC_QT_ADJUST_PANEL_HPP_
#define VLC_QT_ADJUST_PANEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QWidget>
#include <QVariant>
#include <QList>
#include <QTimer>
#include <functional>
#include <QPair>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;
class QToolButton;
class QPushButton;
class QButtonGroup;
class QVariantAnimation;
class QScrollArea;
class AdjustSlider;
class ToggleSwitch;
class CollapsibleSection;
class AudioFilterControlWidget;

/* Four-tab right sidebar replacing "Adjustments and Effects":
 * Playback, Video, Audio, Subtitles (Figma "Subtitles" frame, 400px). */
class AdjustPanel : public QWidget
{
    Q_OBJECT
public:
    enum Tab { PLAYBACK_TAB, VIDEO_TAB, AUDIO_TAB, SUBTITLES_TAB, TAB_COUNT };
    enum { PANEL_WIDTH = 400 };

    AdjustPanel( intf_thread_t *, QWidget *parent );
    int currentTab() const;

public slots:
    void setCurrentTab( int, bool animate = true );

signals:
    void closeRequested();

protected:
    void showEvent( QShowEvent * ) override;
    void keyPressEvent( QKeyEvent * ) override;
    bool eventFilter( QObject *, QEvent * ) override;

private slots:
    void refreshInput();
    void refreshVout();
    void refreshAudioEffects();

private:
    /* tabs */
    QWidget *buildPlaybackTab();
    QWidget *buildVideoTab();
    QWidget *buildAudioTab();
    QWidget *buildSubtitlesTab();
    QWidget *buildFooter();
    void moveUnderline( bool animate );

    /* layout helpers, measured on the Figma frame */
    QScrollArea *makePage( QVBoxLayout ** );
    QVBoxLayout *addSection( QVBoxLayout *page, const QString &title,
                             ToggleSwitch **toggle = NULL );
    QLabel *addRow( QVBoxLayout *, const QString &label, QWidget *right );
    QComboBox *makeCombo( std::function<void()> aboutToShow = nullptr );
    QDoubleSpinBox *makeSpin( double min, double max, int decimals,
                              double step, const QString &suffix );
    AdjustSlider *bindSlider( QVBoxLayout *, QDoubleSpinBox *, double min,
                              double max, double origin, int ticks = 0 );
    void addScale( QVBoxLayout *, const QStringList & );
    CollapsibleSection *addCard( QVBoxLayout *, const QString &key );
    void addOptionSlider( QVBoxLayout *, const QString &label, const char *module,
                          const char *option, double min, double max, double def,
                          int ticks, ToggleSwitch *master,
                          bool zeroDisables = false );
    void addAudioEffect( QVBoxLayout *page, const QString &title,
                         AudioFilterControlWidget * );

    /* vlc helpers */
    void fillTrackCombo( QComboBox *, const char *var );
    void setVoutVar( const char *var, const QVariant & );
    void setSubtitleConfig( const char *option, const QVariant & );

    /* presets: every adjustable value registers a getter/setter */
    struct Control
    {
        QString key;
        std::function<QVariant()> get;
        std::function<void( const QVariant & )> set;
        QVariant def;
    };
    void registerControl( const QString &key, std::function<QVariant()> get,
                          std::function<void( const QVariant & )> set,
                          const QVariant &def );
    void rebuildPresetMenu();
    void applyPreset( const QString &name );
    void savePresetAs();
    void resetToDefaults();

    intf_thread_t *p_intf;
    QStackedWidget *stack;
    QWidget *tabBar;
    QButtonGroup *tabs;
    QWidget *underline;
    QVariantAnimation *underlineAnim;
    QPushButton *presetButton;
    QLabel *presetLabel;
    QList<Control> controls;
    QTimer saveTimer;   /* debounced config_SaveConfigFile */
    QTimer sleepTimer;
    bool b_refreshing;

    /* live widgets refreshed from the input */
    QComboBox *videoTrack, *audioTrack, *subTrack, *audioDevice, *stereoMode;
    QComboBox *aspectCombo, *cropCombo, *deintCombo, *deintModeCombo;
    QComboBox *repeatCombo, *sleepCombo;
    QDoubleSpinBox *rateSpin, *audioDelay, *subDelay, *subFps, *subDuration;
    ToggleSwitch *shuffleSwitch, *stopAfterSwitch;
    QToolButton *abButton;
    QVBoxLayout *lastSection;
    QComboBox *rotateCombo;
    QList<std::function<void()>> voutAppliers; /* re-apply on new vout */
    QList<QPair<ToggleSwitch *, AudioFilterControlWidget *>> audioEffects;
};

/* Host whose width animates while the panel keeps its 400px layout and
 * stays anchored to the right edge, so it slides in without reflowing.
 * The first GAP pixels separate it from the content panel. */
class AdjustDock : public QWidget
{
public:
    enum { GAP = 10 };
    AdjustDock( intf_thread_t *, QWidget *parent );
    AdjustPanel *panel() const { return p; }
    bool isOpen() const { return open; }
    void setOpen( bool, bool animate = true );

protected:
    void resizeEvent( QResizeEvent * ) override;

private:
    AdjustPanel *p;
    QVariantAnimation *anim;
    bool open;
};

#endif
