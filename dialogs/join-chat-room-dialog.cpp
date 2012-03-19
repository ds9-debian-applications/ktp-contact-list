/*
 * This file is part of telepathy-contactslist-prototype
 *
 * Copyright (C) 2011 Francesco Nwokeka <francesco.nwokeka@gmail.com>
 * Copyright (C) 2012 Dominik Cermak <d.cermak@arcor.de>
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

#include "join-chat-room-dialog.h"
#include "ui_join-chat-room-dialog.h"

#include <KTp/Models/accounts-model.h>

#include <KConfig>
#include <KInputDialog>
#include <KMessageBox>
#include <KNotification>
#include <KPushButton>

#include <TelepathyQt/AccountManager>
#include <TelepathyQt/ChannelTypeRoomListInterface>
#include <TelepathyQt/PendingChannel>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/RoomListChannel>

#include <QSortFilterProxyModel>

#include <rooms-model.h>

#include <KDebug>

JoinChatRoomDialog::JoinChatRoomDialog(Tp::AccountManagerPtr accountManager, QWidget* parent)
    : KDialog(parent, Qt::Dialog)
    , ui(new Ui::JoinChatRoomDialog)
    , m_model(new RoomsModel(this))
    , m_favoritesModel(new FavoriteRoomsModel(this))
    , m_favoritesProxyModel(new QSortFilterProxyModel(this)
    )
{
    QWidget *joinChatRoomDialog = new QWidget(this);
    m_accounts = accountManager->allAccounts();
    ui->setupUi(joinChatRoomDialog);
    setMainWidget(joinChatRoomDialog);
    setWindowIcon(KIcon("telepathy-kde"));

    // config
    KSharedConfigPtr config = KSharedConfig::openConfig("ktelepathyrc");
    m_favoriteRoomsGroup = config->group("FavoriteRooms");

    loadFavoriteRooms();

    // disable OK button on start
    button(Ok)->setEnabled(false);
    
    //set icons
    ui->addFavoritePushButton->setIcon(KIcon(QLatin1String("list-add")));
    ui->removeFavoritePushButton->setIcon(KIcon(QLatin1String("list-remove")));
    

    // populate combobox with accounts that support chat rooms
    for (int i = 0; i < m_accounts.count(); ++i) {
        Tp::AccountPtr acc = m_accounts.at(i);

        if (acc->capabilities().textChatrooms() && acc->isOnline()) {
            // add unique data to identify the correct account later on
            ui->comboBox->addItem(KIcon(acc->iconName()), acc->displayName(), acc->uniqueIdentifier());
        }
    }

    // Apply the filter after populating
    onAccountSelectionChanged(ui->comboBox->currentIndex());

    // FavoritesTab
    m_favoritesProxyModel->setSourceModel(m_favoritesModel);
    m_favoritesProxyModel->setFilterKeyColumn(FavoriteRoomsModel::AccountIdentifierColumn);
    m_favoritesProxyModel->setDynamicSortFilter(true);

    ui->listView->setModel(m_favoritesProxyModel);
    ui->listView->setModelColumn(FavoriteRoomsModel::NameColumn);

    // QueryTab
    if (ui->comboBox->count() > 0) {
        ui->queryPushButton->setEnabled(true);
    }

    QSortFilterProxyModel *proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(m_model);
    proxyModel->setSortLocaleAware(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterKeyColumn(RoomsModel::NameColumn);
    proxyModel->setDynamicSortFilter(true);

    ui->treeView->setModel(proxyModel);
    ui->treeView->header()->setResizeMode(QHeaderView::ResizeToContents);
    ui->treeView->sortByColumn(RoomsModel::NameColumn, Qt::AscendingOrder);

    // connects
    connect(ui->lineEdit, SIGNAL(textChanged(QString)), this, SLOT(onTextChanged(QString)));
    connect(ui->listView, SIGNAL(clicked(QModelIndex)), this, SLOT(onFavoriteRoomClicked(QModelIndex)));
    connect(ui->addFavoritePushButton, SIGNAL(clicked(bool)), this, SLOT(addFavorite()));
    connect(ui->removeFavoritePushButton, SIGNAL(clicked(bool)), this, SLOT(removeFavorite()));
    connect(ui->queryPushButton, SIGNAL(clicked(bool)), this, SLOT(getRoomList()));
    connect(ui->stopPushButton, SIGNAL(clicked(bool)), this, SLOT(stopListing()));
    connect(ui->treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(onRoomClicked(QModelIndex)));
    connect(ui->filterBar, SIGNAL(textChanged(QString)), proxyModel, SLOT(setFilterFixedString(QString)));
    connect(ui->comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onAccountSelectionChanged(int)));
}

JoinChatRoomDialog::~JoinChatRoomDialog()
{
    delete ui;
}

Tp::AccountPtr JoinChatRoomDialog::selectedAccount() const
{
    Tp::AccountPtr account;
    bool found = false;

    for (int i = 0; i < m_accounts.count() && !found; ++i) {
        if (m_accounts.at(i)->uniqueIdentifier() == ui->comboBox->itemData(ui->comboBox->currentIndex()).toString()) {
            account = m_accounts.at(i);
            found = true;
        }
    }

    // account should never be empty
    return account;
}

void JoinChatRoomDialog::onAccountSelectionChanged(int newIndex)
{
    QString accountIdentifier = ui->comboBox->itemData(newIndex).toString();
    m_favoritesProxyModel->setFilterFixedString(accountIdentifier);
}

void JoinChatRoomDialog::addFavorite()
{
    bool ok = false;
    QString favoriteHandle = ui->lineEdit->text();
    QString favoriteAccount = ui->comboBox->itemData(ui->comboBox->currentIndex()).toString();

    if (m_favoritesModel->containsRoom(favoriteHandle, favoriteAccount)) {
        KMessageBox::sorry(this, i18n("This room is already in your favorites."));
    } else {
        QString favoriteName = KInputDialog::getText(i18n("Add room"), i18n("Name"), QString(), &ok);

        if (ok) {
            QString key = favoriteHandle + favoriteAccount;

            // Write it to the config file
            QVariantList favorite;
            favorite.append(favoriteName);
            favorite.append(favoriteHandle);
            favorite.append(favoriteAccount);
            m_favoriteRoomsGroup.writeEntry(key, favorite);
            m_favoriteRoomsGroup.sync();

            // Insert it into the model
            QVariantMap room;
            room.insert("name", favoriteName);
            room.insert("handle-name", favoriteHandle);
            room.insert("account-identifier", favoriteAccount);
            m_favoritesModel->addRoom(room);
        }
    }
}

void JoinChatRoomDialog::removeFavorite()
{
    QString favoriteHandle = ui->listView->currentIndex().data(FavoriteRoomsModel::HandleNameRole).toString();
    QString favoriteAccount = ui->comboBox->itemData(ui->comboBox->currentIndex()).toString();
    QVariantMap room = ui->listView->currentIndex().data(FavoriteRoomsModel::FavoriteRoomRole).value<QVariantMap>();

    QString key = favoriteHandle + favoriteAccount;

    if(m_favoriteRoomsGroup.keyList().contains(key)) {
        m_favoriteRoomsGroup.deleteEntry(key);
        m_favoriteRoomsGroup.sync();
        m_favoritesModel->removeRoom(room);

        if (m_favoritesModel->countForAccount(favoriteAccount) == 0) {
            ui->removeFavoritePushButton->setEnabled(false);
        }
    }
}

void JoinChatRoomDialog::getRoomList()
{
    Tp::AccountPtr account = selectedAccount();

    // Clear the list from previous items
    m_model->clearRoomInfoList();

    // Build the channelrequest
    QVariantMap request;
    request.insert(TP_QT_IFACE_CHANNEL + QLatin1String(".ChannelType"),
                   TP_QT_IFACE_CHANNEL_TYPE_ROOM_LIST);
    request.insert(TP_QT_IFACE_CHANNEL + QLatin1String(".TargetHandleType"),
                   Tp::HandleTypeNone);

    // If the user provided a server use it, else use the standard server for the selected account
    if (!ui->serverLineEdit->text().isEmpty()) {
        request.insert(TP_QT_IFACE_CHANNEL + QLatin1String(".Type.RoomList.Server"),
                       ui->serverLineEdit->text());
    }

    m_pendingRoomListChannel = account->createAndHandleChannel(request, QDateTime::currentDateTime());
    connect(m_pendingRoomListChannel, SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(onRoomListChannelReadyForHandling(Tp::PendingOperation*)));

}

void JoinChatRoomDialog::stopListing()
{
    m_iface->StopListing();
}

void JoinChatRoomDialog::onRoomListChannelReadyForHandling(Tp::PendingOperation *operation)
{
    if (operation->isError()) {
        kDebug() << operation->errorName();
        kDebug() << operation->errorMessage();
        QString errorMsg(operation->errorName() + QLatin1String(": ") + operation->errorMessage());
        sendNotificationToUser(errorMsg);
    } else {
        m_roomListChannel = m_pendingRoomListChannel->channel();

        connect(m_roomListChannel->becomeReady(),
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(onRoomListChannelReady(Tp::PendingOperation*)));
    }
}

void JoinChatRoomDialog::onRoomListChannelReady(Tp::PendingOperation *operation)
{
    if (operation->isError()) {
        kDebug() << operation->errorName();
        kDebug() << operation->errorMessage();
        QString errorMsg(operation->errorName() + QLatin1String(": ") + operation->errorMessage());
        sendNotificationToUser(errorMsg);
    } else {
        m_iface = m_roomListChannel->interface<Tp::Client::ChannelTypeRoomListInterface>();

        m_iface->ListRooms();

        connect(m_iface, SIGNAL(ListingRooms(bool)), SLOT(onListing(bool)));
        connect(m_iface, SIGNAL(GotRooms(Tp::RoomInfoList)), SLOT(onGotRooms(Tp::RoomInfoList)));
    }
}

void JoinChatRoomDialog::onRoomListChannelClosed(Tp::PendingOperation *operation)
{
    if (operation->isError()) {
        kDebug() << operation->errorName();
        kDebug() << operation->errorMessage();
        QString errorMsg(operation->errorName() + QLatin1String(": ") + operation->errorMessage());
        sendNotificationToUser(errorMsg);
    } else {
        ui->queryPushButton->setEnabled(true);
        ui->stopPushButton->setEnabled(false);
    }
}

void JoinChatRoomDialog::onListing(bool isListing)
{
    if (isListing) {
        kDebug() << "listing";
        ui->queryPushButton->setEnabled(false);
        ui->stopPushButton->setEnabled(true);
    } else {
        kDebug() << "finished listing";
        Tp::PendingOperation *op =  m_roomListChannel->requestClose();
        connect(op,
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(onRoomListChannelClosed(Tp::PendingOperation*)));
    }
}

void JoinChatRoomDialog::onGotRooms(Tp::RoomInfoList roomInfoList)
{
    m_model->addRooms(roomInfoList);
}

void JoinChatRoomDialog::onFavoriteRoomClicked(const QModelIndex &index)
{
    if (index.isValid()) {
        ui->removeFavoritePushButton->setEnabled(true);
        ui->lineEdit->setText(index.data(FavoriteRoomsModel::HandleNameRole).toString());
    } else {
        ui->removeFavoritePushButton->setEnabled(false);
    }
}

void JoinChatRoomDialog::onRoomClicked(const QModelIndex& index)
{
    ui->lineEdit->setText(index.data(RoomsModel::HandleNameRole).toString());
}

QString JoinChatRoomDialog::selectedChatRoom() const
{
    return ui->lineEdit->text();
}

void JoinChatRoomDialog::onTextChanged(QString newText)
{
    if (newText.isEmpty()) {
        button(Ok)->setEnabled(false);
        ui->addFavoritePushButton->setEnabled(false);
    } else {
        button(Ok)->setEnabled(true);
        ui->addFavoritePushButton->setEnabled(true);
    }
}

void JoinChatRoomDialog::sendNotificationToUser(const QString& errorMsg)
{
    //The pointer is automatically deleted when the event is closed
    KNotification *notification;
    notification = new KNotification(QLatin1String("telepathyError"), this);

    notification->setText(errorMsg);
    notification->sendEvent();
}

void JoinChatRoomDialog::loadFavoriteRooms()
{
    QList<QVariantMap> roomList;

    Q_FOREACH(const QString &key, m_favoriteRoomsGroup.keyList()) {
        QVariantList favorite = m_favoriteRoomsGroup.readEntry(key, QVariantList());
        QString favoriteName = favorite.at(0).toString();
        QString favoriteHandle = favorite.at(1).toString();
        QString favoriteAccount = favorite.at(2).toString();

        QVariantMap room;
        room.insert("name", favoriteName);
        room.insert("handle-name", favoriteHandle);
        room.insert("account-identifier", favoriteAccount);
        roomList.append(room);
    }

    m_favoritesModel->addRooms(roomList);
}
