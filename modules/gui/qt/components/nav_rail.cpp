/*****************************************************************************
 * nav_rail.cpp : redesigned left navigation rail
 *****************************************************************************
 * Copyright (C) 2026 the VideoLAN team
 *
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

#include "components/nav_rail.hpp"
#include "styles/vlc_theme.hpp"

#include <QToolButton>
#include <QVBoxLayout>

static const struct
{
    const char *key;
    const char *icon;
    const char *title;
} sections[] = {
    { "home",      "home",     N_("Home") },
    { "videos",    "video",    N_("Videos") },
    { "music",     "music",    N_("Music") },
    { "network",   "network",  N_("Network") },
    { "playlists", "playlist", N_("Playlists") },
    { "recents",   "recent",   N_("Recent") },
};

QString NavRail::titleFor( const QString &key )
{
    for( size_t i = 0; i < sizeof( sections ) / sizeof( sections[0] ); i++ )
        if( key == sections[i].key )
            return qtr( sections[i].title );
    return QString();
}

NavRail::NavRail( QWidget *parent ) : QWidget( parent )
{
    setObjectName( "VLCRail" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFixedWidth( Theme::RAIL_WIDTH );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 1, 0, 8 );
    layout->setSpacing( 0 );

    for( size_t i = 0; i < sizeof( sections ) / sizeof( sections[0] ); i++ )
    {
        const QString key = sections[i].key;
        QToolButton *b = new QToolButton( this );
        Theme::setupIconButton( b, sections[i].icon, Theme::RAIL_WIDTH, Theme::RAIL_GLYPH );
        b->setFixedSize( Theme::RAIL_WIDTH, Theme::RAIL_ITEM_HEIGHT );
        b->setCheckable( true );
        b->setToolTip( qtr( sections[i].title ) );
        connect( b, &QToolButton::clicked, this, [this, key]() {
            if( currentKey == key )
            {
                setCurrent( QString() );
                emit collapseRequested();
            }
            else
            {
                setCurrent( key );
                emit sectionActivated( key );
            }
        } );
        buttons.insert( key, b );
        layout->addWidget( b );
    }

    QToolButton *add = new QToolButton( this );
    Theme::setupIconButton( add, "add", Theme::RAIL_WIDTH, Theme::RAIL_GLYPH );
    add->setFixedSize( Theme::RAIL_WIDTH, Theme::RAIL_ITEM_HEIGHT );
    add->setToolTip( qtr( "Add media" ) );
    connect( add, &QToolButton::clicked, this, &NavRail::addRequested );
    layout->addWidget( add );
    layout->addStretch( 1 );
}

void NavRail::setCurrent( const QString &key )
{
    currentKey = key;
    for( auto it = buttons.begin(); it != buttons.end(); ++it )
        it.value()->setChecked( it.key() == key );
}
