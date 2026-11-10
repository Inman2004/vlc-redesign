/*****************************************************************************
 * shortcuts_dialog.hpp : the redesigned Keyboard Shortcuts dialog (P6b)
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

#ifndef VLC_QT_SHORTCUTS_DIALOG_HPP_
#define VLC_QT_SHORTCUTS_DIALOG_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/redesign_dialogs.hpp"

#include <QHash>
#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;

/* One binding cell: the key(s) centred, a pencil on the right. A click
 * starts capturing; the next key press becomes the binding, Backspace
 * clears it, Esc cancels. */
class KeyBox : public QWidget
{
    Q_OBJECT
public:
    KeyBox( const QString &option, QWidget *parent );
    QString option() const { return opt; }
    /* tab-separated VLC key names, as stored in the configuration */
    QString value() const { return val; }
    void setValue( const QString & );
    void setConflict( const QString &otherAction );
    void setAvailable( bool, const QString &reason );
    /* "Ctrl+Q", "Space", "F": what the cell shows */
    QString display() const;
    static QString pretty( const QString &stored );

signals:
    void captured( KeyBox *, const QString &newValue );

protected:
    bool event( QEvent * ) Q_DECL_OVERRIDE;
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
    void focusOutEvent( QFocusEvent * ) Q_DECL_OVERRIDE;

private:
    QString opt, val, conflict;
    bool capturing, hovered, available;
};

/* Figma row 3 "Keyboard Shortcuts": search + field filter, then every
 * key-* option in sections with its Hotkey and Global Hotkey. Save stays
 * disabled until something changes; saved keys work at once (KeyMap). */
class ShortcutsDialog : public RedesignDialog
{
    Q_OBJECT
public:
    ShortcutsDialog( intf_thread_t *, QWidget *parent );

public slots:
    void accept() Q_DECL_OVERRIDE;

private:
    struct Row
    {
        QWidget *widget;
        QString label;
        KeyBox *hotkey, *global;
        int section;
    };
    struct Section
    {
        QWidget *heading;   /* rule + title */
        QWidget *rule;      /* NULL on the first section */
        QWidget *title;
        QList<int> rows;
    };

    void build();
    void onCaptured( KeyBox *, const QString & );
    void updateConflicts();
    void updateDirty();
    void filter();

    QLineEdit *search;
    QComboBox *field;
    QList<Row> rows;
    QList<Section> sections;
    QHash<QString, QString> original;   /* option -> stored value */
    QHash<KeyBox *, int> rowOf;
};

#endif
