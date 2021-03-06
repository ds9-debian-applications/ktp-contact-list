/*
 *  Contact List Widget
 *  Copyright (C) 2011  Martin Klapetek <martin.klapetek@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef CONTACT_LIST_WIDGET_H
#define CONTACT_LIST_WIDGET_H

#include <QTreeView>
#include <TelepathyQt/Types>
#include <TelepathyQt/Connection>
#include <KTp/Models/contacts-model.h>

class ContactListWidgetPrivate;

namespace Tp {
    class PendingOperation;
}

class ContactListWidget : public QTreeView
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(ContactListWidget)
    Q_DISABLE_COPY(ContactListWidget)

public:
    explicit ContactListWidget(QWidget* parent);
    virtual ~ContactListWidget();

    void setAccountManager(const Tp::AccountManagerPtr &accountManager);
    void toggleGroups(bool show);

    KTp::ContactsModel* contactsModel() const;

public Q_SLOTS:
    void toggleOfflineContacts(bool show);
    void showGrouped();
    void showUngrouped();
    void toggleSortByPresence(bool sort);

    void setFilterString(const QString &string);
    void showSettingsKCM();

    void onSwitchToFullView();
    void onSwitchToCompactView();
    void onSwitchToMiniView();

    void onShowAllContacts();
    void onShowUnblockedContacts();
    void onShowBlockedContacts();

private Q_SLOTS:
    void onContactListClicked(const QModelIndex &index);
    void onContactListDoubleClicked(const QModelIndex &index);

    void onNewGroupModelItemsInserted(const QModelIndex &parentIndex, int start, int end);
    void addOverlayButtons();

    void startTextChannel(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void startFileTransferChannel(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void startAudioChannel(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void startVideoChannel(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void startDesktopSharing(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void startLogViewer(const Tp::AccountPtr &account, const Tp::ContactPtr &contact);
    void accountEnablingFinished(Tp::PendingOperation *op);

Q_SIGNALS:
    void enableOverlays(bool);
    void accountManagerReady(Tp::PendingOperation* op);
    void genericOperationFinished(Tp::PendingOperation* op);
    void actionStarted();
    void contactSelectionChanged();

protected:
    void setDropIndicatorRect(const QRect &rect);
    virtual bool event(QEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void paintEvent(QPaintEvent *event);
    virtual void dropEvent(QDropEvent *event);
    virtual void dragEnterEvent(QDragEnterEvent *event);
    virtual void dragMoveEvent(QDragMoveEvent *event);
    virtual void dragLeaveEvent(QDragLeaveEvent *event);
    virtual void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const;

private:
    /** Internally set the group mode used by the model. Note this function does not update d->groupMode*/
    void setGroupMode(KTp::ContactsModel::GroupMode groupMode);

    void requestFileTransferChannels(const Tp::AccountPtr &account,
                                     const Tp::ContactPtr &contact,
                                     const QStringList &filenames);

    void loadGroupStatesFromConfig();

    friend class ContextMenu;
    ContactListWidgetPrivate * const d_ptr;
};

#endif // CONTACT_LIST_WIDGET_H
