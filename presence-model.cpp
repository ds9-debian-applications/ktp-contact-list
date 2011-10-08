/*
 * Presence Model - A model of settable presences.
 *
 * Copyright (C) 2011 David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "presence-model.h"

#include <QtGui/QFont>

#include <KDE/KIcon>
#include <KDE/KLocalizedString>

#include <KDE/KConfig>
#include <KDE/KConfigGroup>
#include <KDE/KGlobalSettings>



PresenceModel::PresenceModel(QObject *parent) :
    QAbstractListModel(parent)
{
    KSharedConfigPtr config = KSharedConfig::openConfig("telepathy-kde-contactlistrc");
    m_presenceGroup = config->group("Custom Presence List");

    loadDefaultPresences();
    loadCustomPresences();
}

PresenceModel::~PresenceModel()
{
    Q_FOREACH(const KPresence &presence, m_presences) {
        if (!presence.statusMessage().isEmpty()) {
            QVariantList presenceVariant;
            presenceVariant.append(presence.type());
            presenceVariant.append(presence.statusMessage());
            QString id = QString::number(presence.type()).append(presence.statusMessage());
            m_presenceGroup.writeEntry(id, presenceVariant);
        }
    }
    m_presenceGroup.sync();
}

QVariant PresenceModel::data(const QModelIndex &index, int role) const
{
    KPresence presence = m_presences[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        if (presence.statusMessage().isEmpty()) {
            switch (presence.type()) {
            case Tp::ConnectionPresenceTypeAvailable:
                return i18n("Available");
            case Tp::ConnectionPresenceTypeBusy:
                return i18n("Busy");
            case Tp::ConnectionPresenceTypeAway:
                return i18n("Away");
            case Tp::ConnectionPresenceTypeExtendedAway:
                return i18n("Extended Away");
            case Tp::ConnectionPresenceTypeHidden:
                return i18n("Invisible");
            case Tp::ConnectionPresenceTypeOffline:
                return i18n("Offline");
            default:
                return i18n("Unknown");
            }
        } else {
            return presence.statusMessage();
        }

    case Qt::DecorationRole:
        return presence.icon();

    case Qt::FontRole:
        if (presence.statusMessage().isEmpty()) {
            QFont font = KGlobalSettings::generalFont();
            font.setBold(true);
            return font;
        }

    case PresenceModel::PresenceRole:
        return QVariant::fromValue<Tp::Presence>(presence);

    }

    return QVariant();
}

int PresenceModel::rowCount(const QModelIndex &parent) const
{
    return m_presences.size();
}

void PresenceModel::loadDefaultPresences()
{
    addPresence(Tp::Presence::available());
    addPresence(Tp::Presence::busy());
    addPresence(Tp::Presence::away());
    addPresence(Tp::Presence::xa());
    addPresence(Tp::Presence::hidden());
    addPresence(Tp::Presence::offline());
}


void PresenceModel::loadCustomPresences()
{
    Q_FOREACH(const QString key, m_presenceGroup.keyList()) {
        QVariantList entry = m_presenceGroup.readEntry(key, QVariantList());

        QString statusMessage = entry.last().toString();

        switch (entry.first().toInt()) {
        case Tp::ConnectionPresenceTypeAvailable:
            addPresence(Tp::Presence::available(statusMessage));
            break;
        case Tp::ConnectionPresenceTypeAway:
            addPresence(Tp::Presence::away(statusMessage));
            break;
        case Tp::ConnectionPresenceTypeBusy:
            addPresence(Tp::Presence::busy(statusMessage));
            break;
        }
    }
}

QModelIndex PresenceModel::addPresence(const Tp::Presence &presence)
{
    QList<KPresence>::iterator i = qLowerBound(m_presences.begin(), m_presences.end(), KPresence(presence));

    m_presences.insert(i, presence);
    int index = m_presences.indexOf(presence);
    //this is technically a backwards and wrong, but I can't get a row from a const iterator, and using qLowerBound does seem a good approach
    beginInsertRows(QModelIndex(), index, index);
    endInsertRows();

    return createIndex(index, 0);
}

void PresenceModel::removePresence(const Tp::Presence &presence)
{
    int row = m_presences.indexOf(presence);
    beginRemoveRows(QModelIndex(), row, row);
    m_presences.removeOne(presence);
    endRemoveRows();
    QString id = QString::number(presence.type()).append(presence.statusMessage());

    if(m_presenceGroup.keyList().contains(id)) {
        m_presenceGroup.deleteEntry(id);
        m_presenceGroup.sync();
    }
}

