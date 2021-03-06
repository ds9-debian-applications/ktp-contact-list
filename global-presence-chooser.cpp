/*
 * Global Presence - A Drop down menu for selecting presence
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

#include "global-presence-chooser.h"

#include <KTp/global-presence.h>
#include <KTp/presence.h>
#include <KTp/Models/presence-model.h>

#include "dialogs/custom-presence-dialog.h"

#include <KLocalizedString>
#include <KSharedConfig>
#include <KLineEdit>
#include <KPixmapSequence>
#include <KPixmapSequenceOverlayPainter>
#include <KMessageBox>
#include <KIconLoader>

#include <TelepathyQt/Presence>
#include <TelepathyQt/Account>

#include <QFontDatabase>
#include <QMouseEvent>
#include <QToolTip>
#include <QStyle>
#include <QPushButton>
#include <QMenu>
#include <QPointer>

//A sneaky class that adds an extra entries to the end of the presence model,
//currently "Now listening to" and "Configure Custom Presences"
class PresenceModelExtended : public QAbstractListModel
{
    Q_OBJECT
public:
    PresenceModelExtended(KTp::PresenceModel *presenceModel, QObject *parent);
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    KTp::Presence temporaryPresence() const;
    /** Adds a presence to the model which is to be used when the presence has been set externally and we need to show it, but not save it to the config*/
    QModelIndex addTemporaryPresence(const KTp::Presence &presence);
    void removeTemporaryPresence();
private slots:
    void sourceRowsInserted(const QModelIndex &index, int start, int end);
    void sourceRowsRemoved(const QModelIndex &index, int start, int end);
private:
    KTp::Presence m_temporaryPresence;
    KTp::PresenceModel *m_model;
};

PresenceModelExtended::PresenceModelExtended(KTp::PresenceModel *presenceModel, QObject *parent) :
    QAbstractListModel(parent),
    m_model(presenceModel)
{
    connect(m_model, SIGNAL(rowsInserted(QModelIndex,int,int)), SLOT(sourceRowsInserted(QModelIndex,int,int)));
    connect(m_model, SIGNAL(rowsRemoved(QModelIndex,int,int)), SLOT(sourceRowsRemoved(QModelIndex,int,int)));
}

//return number of rows + the extra items added to end of list
int PresenceModelExtended::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    int rowCount = m_model->rowCount(parent) + 2;
    if (m_temporaryPresence.isValid()) {
        rowCount++;
    }
    return rowCount;
}

QVariant PresenceModelExtended::data(const QModelIndex &index, int role) const
{
    if (role == Qt::SizeHintRole) {
        const QFontMetrics fontMetrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        return QSize(0, qMax(fontMetrics.height(), (int)(KIconLoader::SizeSmall)) + 8);
    }
    if (index.row() == rowCount() - 1) {
        switch (role) {
        case Qt::DisplayRole:
            return i18n("Configure Custom Presences...");
        case Qt::DecorationRole:
            return QIcon::fromTheme("configure");
        }
    } else if (index.row() == rowCount() - 2) {
        switch (role) {
        case Qt::DisplayRole:
            return i18n("Now listening to...");
        case Qt::DecorationRole:
            return QIcon::fromTheme("speaker");
        }
    } else if (m_temporaryPresence.isValid() && index.row() == rowCount() - 3) {
        switch (role) {
        case Qt::DisplayRole:
            return m_temporaryPresence.statusMessage();
        case Qt::DecorationRole:
            return m_temporaryPresence.icon();
        case KTp::PresenceModel::PresenceRole:
            return QVariant::fromValue<KTp::Presence>(m_temporaryPresence);
        }
    } else {
        return m_model->data(m_model->index(index.row()), role);
    }
    return QVariant();
}

KTp::Presence PresenceModelExtended::temporaryPresence() const
{
    return m_temporaryPresence;
}

void PresenceModelExtended::sourceRowsInserted(const QModelIndex &index, int start, int end)
{
    beginInsertRows(createIndex(index.row(), 0), start, end);
    endInsertRows();
}

void PresenceModelExtended::sourceRowsRemoved(const QModelIndex &index, int start, int end)
{
    beginRemoveRows(createIndex(index.row(), 0), start, end);
    endRemoveRows();
}

QModelIndex PresenceModelExtended::addTemporaryPresence(const KTp::Presence &presence)
{
    int row = m_model->rowCount();

    //if the temp presence already exists, don't remove and readd it
    //but simply replace it
    if (m_temporaryPresence.isValid()) {
        m_temporaryPresence = presence;
        emit dataChanged(this->createIndex(row, 0), this->createIndex(row, 0));
    } else {
        beginInsertRows(QModelIndex(), row, row);
        m_temporaryPresence = presence;
        endInsertRows();
    }

    return this->createIndex(row, 0);
}

void PresenceModelExtended::removeTemporaryPresence()
{
    if (!m_temporaryPresence.isValid()) {
        return; //if not already set, do nothing.
    }

    int row = m_model->rowCount();
    beginRemoveRows(QModelIndex(), row, row);
    m_temporaryPresence = KTp::Presence();
    endRemoveRows();
}

//----------------------------------------------------------------------------------------------------------

GlobalPresenceChooser::GlobalPresenceChooser(QWidget *parent) :
    KComboBox(parent),
    m_globalPresence(new KTp::GlobalPresence(this)),
    m_model(new KTp::PresenceModel(this)),
    m_modelExtended(new PresenceModelExtended(m_model, this)),
    m_busyOverlay(new KPixmapSequenceOverlayPainter(this)),
    m_changePresenceMessageButton(new QPushButton(this))
{
    this->setModel(m_modelExtended);
    //needed for mousemove events
    setMouseTracking(true);

    m_busyOverlay->setSequence(KIconLoader::global()->loadPixmapSequence("process-working", KIconLoader::SizeSmallMedium));
    setEditable(false);

    m_changePresenceMessageButton->setIcon(QIcon::fromTheme("document-edit"));
    m_changePresenceMessageButton->setFlat(true);
    m_changePresenceMessageButton->setToolTip(i18n("Click to change your presence message"));

    connect(this, SIGNAL(currentIndexChanged(int)), SLOT(onAllComboChanges(int)));
    connect(this, SIGNAL(activated(int)), SLOT(onUserActivatedComboChange(int)));
    connect(m_globalPresence, SIGNAL(requestedPresenceChanged(KTp::Presence)), SLOT(onPresenceChanged(KTp::Presence)));
    connect(m_globalPresence, SIGNAL(connectionStatusChanged(Tp::ConnectionStatus)), SLOT(onConnectionStatusChanged(Tp::ConnectionStatus)));
    connect(m_changePresenceMessageButton, SIGNAL(clicked(bool)), this, SLOT(onChangePresenceMessageClicked()));

    onPresenceChanged(m_globalPresence->requestedPresence());
    //we need to check if there is some account connecting and if so, spin the spinner
    onConnectionStatusChanged(m_globalPresence->connectionStatus());
}

void GlobalPresenceChooser::setAccountManager(const Tp::AccountManagerPtr &accountManager)
{
    m_accountManager = accountManager;
    m_globalPresence->setAccountManager(accountManager);
}

bool GlobalPresenceChooser::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        if (m_accountManager.isNull()) {
            return false;
        }

        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(e);

        QString toolTipText;
        toolTipText.append("<table>");

        Q_FOREACH(const Tp::AccountPtr &account, m_accountManager->allAccounts()) {
            if (account->isEnabled()) {
                KTp::Presence accountPresence(account->currentPresence());
                QString presenceIconPath = KIconLoader::global()->iconPath(accountPresence.icon().name(), 1);
                QString presenceIconString = QString::fromLatin1("<img src=\"%1\">").arg(presenceIconPath);
                QString accountIconPath = KIconLoader::global()->iconPath(account->iconName(), 1);
                QString accountIconString = QString::fromLatin1("<img src=\"%1\" width=\"%2\" height=\"%2\">").arg(accountIconPath).arg(KIconLoader::SizeSmallMedium);
                QString presenceString;
                if (account->connectionStatus() == Tp::ConnectionStatusConnecting) {
                    presenceString = i18nc("Presence string when the account is connecting", "Connecting...");
                } else {
                    presenceString = accountPresence.displayString();
                }
                toolTipText.append(QString::fromLatin1("<tr><td>%1 %2</td></tr><tr><td style=\"padding-left: 24px\">%3&nbsp;%4</td></tr>").arg(accountIconString, account->displayName(), presenceIconString, presenceString));
            }
        }

        toolTipText.append("</table>");
        QToolTip::showText(helpEvent->globalPos(), toolTipText, this);
        return true;
    }

    if (e->type() == QEvent::Resize) {
        repositionOverlays();
    }

    if (e->type() == QEvent::ContextMenu) {
        QMouseEvent *me = static_cast<QMouseEvent*>(e);
        if (isEditable()) {
            //we need to correctly position the menu, otherwise it just appears at (0;0)
            m_lineEditContextMenu.data()->exec(me->globalPos());

            return true;
        }
    }

    if (e->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(e);

        if (ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Return) {
            if (isEditable()) {
                onConfirmPresenceMessageClicked();
                return true;
            }
        }
        if (ke->key() == Qt::Key_Escape) {
            if (isEditable()) {
                setEditable(false);
                m_changePresenceMessageButton->show();
            }
        }
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
            if (!isEditable()) {
                showPopup();
                return true;
            }
        }
    }

    if (e->type() == QEvent::FocusOut) {
        //just cancel editable and let it exec parent event()
        if (!m_lineEditContextMenu.isNull()) {
            if (!m_lineEditContextMenu.data()->isHidden()) {
                //if we're showing the context menu, do not process this event further
                return true;
            }
            //...otherwise delete the menu and hide the lineedit
            m_lineEditContextMenu.data()->deleteLater();
        }
        if (isEditable()) {
            setEditable(false);
            m_changePresenceMessageButton->show();
        }
    }

    return KComboBox::event(e); // krazy:exclude=qclasses
}

void GlobalPresenceChooser::setEditable(bool editable)
{
    if (editable) {
        m_busyOverlay->setWidget(0);
    } else {
        m_busyOverlay->setWidget(this);
        if (m_globalPresence->connectionStatus() == Tp::ConnectionStatusConnecting) {
            m_busyOverlay->start(); // If telepathy is still connecting, overlay must be spinning again.
        }
    }
    KComboBox::setEditable(editable);
}

void GlobalPresenceChooser::onUserActivatedComboChange(int index)
{
    if (index == -1) {
        return;
    }
    //if they select the "configure item"
    if (index == count() - 1) {
        QPointer<CustomPresenceDialog> dialog = new CustomPresenceDialog(m_model, this);
        dialog.data()->exec();
        delete dialog.data();
        onPresenceChanged(m_globalPresence->requestedPresence());
    } else if (index == count() - 2) {
        KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktelepathyrc"));
        KConfigGroup kdedConfig = config->group("KDED");

        bool pluginEnabled = kdedConfig.readEntry("nowPlayingEnabled", false);

        if (!pluginEnabled) {
            if (KMessageBox::questionYesNo(this,
                                           i18n("This plugin is currently disabled. Do you want to enable it and use as your presence?"),
                                           i18n("Plugin disabled")) == KMessageBox::Yes) {

                kdedConfig.writeEntry("nowPlayingEnabled", true);
                kdedConfig.sync();

                QDBusMessage message = QDBusMessage::createSignal(QLatin1String("/Telepathy"),
                                       QLatin1String("org.kde.Telepathy"),
                                       QLatin1String("settingsChange"));
                QDBusConnection::sessionBus().send(message);
            } else {
                onPresenceChanged(m_globalPresence->requestedPresence());
                return;
            }
        }

        QDBusMessage message = QDBusMessage::createSignal(QLatin1String("/Telepathy"),
							  QLatin1String("org.kde.Telepathy"),
							  QLatin1String("activateNowPlaying"));
        QDBusConnection::sessionBus().send(message);
        onPresenceChanged(m_globalPresence->requestedPresence());
    } else if (m_modelExtended->temporaryPresence().isValid() && index == count() - 3) {
        //do nothing if the temporary presence is selected. This is only used for externally set presences.
        //at which point reselecting it does nothing.
    } else {
        QDBusMessage message = QDBusMessage::createSignal(QLatin1String("/Telepathy"),
							  QLatin1String("org.kde.Telepathy"),
							  QLatin1String("deactivateNowPlaying"));
        QDBusConnection::sessionBus().send(message);
        onPresenceChanged(m_globalPresence->requestedPresence());
        // only set global presence on user change
        KTp::Presence presence = itemData(index, KTp::PresenceModel::PresenceRole).value<KTp::Presence>();
        m_globalPresence->setPresence(presence);
    }
}

void GlobalPresenceChooser::onAllComboChanges(int index)
{
    int lastPresenceIndex = m_model->rowCount();
    if (index < lastPresenceIndex) {
        KTp::Presence presence = itemData(index, KTp::PresenceModel::PresenceRole).value<KTp::Presence>();
        if ((presence.type() == Tp::ConnectionPresenceTypeOffline) ||
                (presence.type() == Tp::ConnectionPresenceTypeHidden)) {
            m_changePresenceMessageButton->hide();
        } else {
            m_changePresenceMessageButton->show();
        }
    }

}


void GlobalPresenceChooser::onPresenceChanged(const KTp::Presence &presence)
{
    if (presence.type() == Tp::ConnectionPresenceTypeUnknown) {
        setCurrentIndex(-1);
        m_busyOverlay->start();
        return;
    }
    for (int i = 0; i < count() ; i++) {
        KTp::Presence itemPresence = itemData(i, KTp::PresenceModel::PresenceRole).value<KTp::Presence>();
        if (itemPresence.type() == presence.type() && itemPresence.statusMessage() == presence.statusMessage()) {
            setCurrentIndex(i);
            if (itemPresence != m_modelExtended->temporaryPresence()) {
                m_modelExtended->removeTemporaryPresence();
            }
            return;
        }
    }

    QModelIndex index = m_modelExtended->addTemporaryPresence(presence);
    setCurrentIndex(index.row());
    m_busyOverlay->stop();
}

void GlobalPresenceChooser::onConnectionStatusChanged(Tp::ConnectionStatus connectionStatus)
{
    if (connectionStatus == Tp::ConnectionStatusConnecting) {
        repositionOverlays();
        m_busyOverlay->start();
    } else {
        m_busyOverlay->stop();
    }
}

void GlobalPresenceChooser::repositionOverlays()
{
    //set 2px margins so that the button is not bigger than the combo
    m_changePresenceMessageButton->setMaximumHeight(height() - 2);
    m_changePresenceMessageButton->setMaximumWidth(height() - 2);
    QPoint topLeft;
    if (m_changePresenceMessageButton->layoutDirection() == Qt::RightToLeft) {
        //move the button 22px from the left edge
        m_changePresenceMessageButton->move(22, 0);
        //place the spinner 2px right from the button, 4 is added to take the margin.
        topLeft.setX(m_changePresenceMessageButton->pos().x() + m_busyOverlay->sequence().frameSize().width() + 4);
        topLeft.setY((height() - m_busyOverlay->sequence().frameSize().height()) / 2);
    } else {
        //move the button 22px from the right edge
        m_changePresenceMessageButton->move(width() - m_changePresenceMessageButton->width() - 22, 0);
        //place the spinner 2px left from the button
        topLeft.setX(m_changePresenceMessageButton->pos().x() - m_busyOverlay->sequence().frameSize().width() - 2);
        topLeft.setY((height() - m_busyOverlay->sequence().frameSize().height()) / 2);
    }
    m_busyOverlay->setRect(QRect(topLeft, m_busyOverlay->sequence().frameSize()));
}

void GlobalPresenceChooser::onChangePresenceMessageClicked()
{
    m_changePresenceMessageButton->hide();

    setEditable(true);

    //if current presence has no presence message, delete the text
    if (m_globalPresence->requestedPresence().statusMessage().isEmpty()) {
        lineEdit()->clear();
    }

    m_lineEditContextMenu = lineEdit()->createStandardContextMenu();

    lineEdit()->setFocus();
}

void GlobalPresenceChooser::onConfirmPresenceMessageClicked()
{
    m_changePresenceMessageButton->show();

    KTp::Presence presence = itemData(currentIndex(), KTp::PresenceModel::PresenceRole).value<KTp::Presence>();
    presence.setStatus(presence.type(), presence.status(), lineEdit()->text());
    QModelIndex newPresence = m_model->addPresence(presence); //m_model->addPresence(presence);
    setEditable(false);
    setCurrentIndex(newPresence.row());

    onUserActivatedComboChange(newPresence.row());
    onAllComboChanges(newPresence.row());
}


#include "global-presence-chooser.moc"
#include "moc_global-presence-chooser.cpp" //hack because we have two QObjects in the same file
