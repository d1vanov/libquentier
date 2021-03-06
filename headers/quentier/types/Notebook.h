/*
 * Copyright 2016-2020 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_QUENTIER_TYPES_NOTEBOOK_H
#define LIB_QUENTIER_TYPES_NOTEBOOK_H

#include "IFavoritableDataElement.h"

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SharedNotebook)
QT_FORWARD_DECLARE_CLASS(User)
QT_FORWARD_DECLARE_CLASS(NotebookData)

class QUENTIER_EXPORT Notebook : public IFavoritableDataElement
{
public:
    QN_DECLARE_LOCAL_UID
    QN_DECLARE_DIRTY
    QN_DECLARE_LOCAL
    QN_DECLARE_FAVORITED

public:
    explicit Notebook();
    Notebook(const Notebook & other);
    Notebook(Notebook && other);
    Notebook & operator=(const Notebook & other);
    Notebook & operator=(Notebook && other);

    explicit Notebook(const qevercloud::Notebook & other);
    explicit Notebook(qevercloud::Notebook && other);
    Notebook & operator=(const qevercloud::Notebook & other);
    Notebook & operator=(qevercloud::Notebook && other);

    virtual ~Notebook() override;

    bool operator==(const Notebook & other) const;
    bool operator!=(const Notebook & other) const;

    const qevercloud::Notebook & qevercloudNotebook() const;
    qevercloud::Notebook & qevercloudNotebook();

    virtual void clear() override;

    static bool validateName(
        const QString & name, ErrorString * pErrorDescription = nullptr);

    virtual bool hasGuid() const override;
    virtual const QString & guid() const override;
    virtual void setGuid(const QString & guid) override;

    virtual bool hasUpdateSequenceNumber() const override;
    virtual qint32 updateSequenceNumber() const override;
    virtual void setUpdateSequenceNumber(const qint32 usn) override;

    virtual bool checkParameters(ErrorString & errorDescription) const override;

    bool hasName() const;
    const QString & name() const;
    void setName(const QString & name);

    bool isDefaultNotebook() const;
    void setDefaultNotebook(const bool defaultNotebook);

    bool hasLinkedNotebookGuid() const;
    const QString & linkedNotebookGuid() const;
    void setLinkedNotebookGuid(const QString & linkedNotebookGuid);

    bool hasCreationTimestamp() const;
    qint64 creationTimestamp() const;
    void setCreationTimestamp(const qint64 timestamp);

    bool hasModificationTimestamp() const;
    qint64 modificationTimestamp() const;
    void setModificationTimestamp(const qint64 timestamp);

    bool hasPublishingUri() const;
    const QString & publishingUri() const;
    void setPublishingUri(const QString & uri);

    bool hasPublishingOrder() const;
    qint8 publishingOrder() const;
    void setPublishingOrder(const qint8 order);

    bool hasPublishingAscending() const;
    bool isPublishingAscending() const;
    void setPublishingAscending(const bool ascending);

    bool hasPublishingPublicDescription() const;
    const QString & publishingPublicDescription() const;
    void setPublishingPublicDescription(
        const QString & publishingPublicDescription);

    bool hasPublished() const;
    bool isPublished() const;
    void setPublished(const bool published);

    bool hasStack() const;
    const QString & stack() const;
    void setStack(const QString & stack);

    bool hasSharedNotebooks();
    QList<SharedNotebook> sharedNotebooks() const;
    void setSharedNotebooks(QList<qevercloud::SharedNotebook> sharedNotebooks);
    void setSharedNotebooks(QList<SharedNotebook> && notebooks);
    void addSharedNotebook(const SharedNotebook & sharedNotebook);
    void removeSharedNotebook(const SharedNotebook & sharedNotebook);

    bool hasBusinessNotebookDescription() const;
    const QString & businessNotebookDescription() const;

    void setBusinessNotebookDescription(
        const QString & businessNotebookDescription);

    bool hasBusinessNotebookPrivilegeLevel() const;
    qint8 businessNotebookPrivilegeLevel() const;
    void setBusinessNotebookPrivilegeLevel(const qint8 privilegeLevel);

    bool hasBusinessNotebookRecommended() const;
    bool isBusinessNotebookRecommended() const;
    void setBusinessNotebookRecommended(const bool recommended);

    bool hasContact() const;
    const User contact() const;
    void setContact(const User & contact);

    bool isLastUsed() const;
    void setLastUsed(const bool lastUsed);

    bool canReadNotes() const;
    void setCanReadNotes(const bool canReadNotes);

    bool canCreateNotes() const;
    void setCanCreateNotes(const bool canCreateNotes);

    bool canUpdateNotes() const;
    void setCanUpdateNotes(const bool canUpdateNotes);

    bool canExpungeNotes() const;
    void setCanExpungeNotes(const bool canExpungeNotes);

    bool canShareNotes() const;
    void setCanShareNotes(const bool canShareNotes);

    bool canEmailNotes() const;
    void setCanEmailNotes(const bool canEmailNotes);

    bool canSendMessageToRecipients() const;
    void setCanSendMessageToRecipients(const bool canSendMessageToRecipients);

    bool canUpdateNotebook() const;
    void setCanUpdateNotebook(const bool canUpdateNotebook);

    bool canExpungeNotebook() const;
    void setCanExpungeNotebook(const bool canExpungeNotebook);

    bool canSetDefaultNotebook() const;
    void setCanSetDefaultNotebook(const bool canSetDefaultNotebook);

    bool canSetNotebookStack() const;
    void setCanSetNotebookStack(const bool canSetNotebookStack);

    bool canPublishToPublic() const;
    void setCanPublishToPublic(const bool canPublishToPublic);

    bool canPublishToBusinessLibrary() const;
    void setCanPublishToBusinessLibrary(const bool canPublishToBusinessLibrary);

    bool canCreateTags() const;
    void setCanCreateTags(const bool canCreateTags);

    bool canUpdateTags() const;
    void setCanUpdateTags(const bool canUpdateTags);

    bool canExpungeTags() const;
    void setCanExpungeTags(const bool canExpungeTags);

    bool canSetParentTag() const;
    void setCanSetParentTag(const bool canSetParentTag);

    bool canCreateSharedNotebooks() const;
    void setCanCreateSharedNotebooks(const bool canCreateSharedNotebooks);

    bool canShareNotesWithBusiness() const;
    void setCanShareNotesWithBusiness(const bool canShareNotesWithBusiness);

    bool canRenameNotebook() const;
    void setCanRenameNotebook(const bool canRenameNotebook);

    bool hasUpdateWhichSharedNotebookRestrictions() const;
    qint8 updateWhichSharedNotebookRestrictions() const;
    void setUpdateWhichSharedNotebookRestrictions(const qint8 which);

    bool hasExpungeWhichSharedNotebookRestrictions() const;
    qint8 expungeWhichSharedNotebookRestrictions() const;
    void setExpungeWhichSharedNotebookRestrictions(const qint8 which);

    bool hasRestrictions() const;
    const qevercloud::NotebookRestrictions & restrictions() const;

    void setNotebookRestrictions(
        qevercloud::NotebookRestrictions && restrictions);

    bool hasRecipientReminderNotifyEmail() const;
    bool recipientReminderNotifyEmail() const;
    void setRecipientReminderNotifyEmail(const bool notifyEmail);

    bool hasRecipientReminderNotifyInApp() const;
    bool recipientReminderNotifyInApp() const;
    void setRecipientReminderNotifyInApp(const bool notifyInApp);

    bool hasRecipientInMyList() const;
    bool recipientInMyList() const;
    void setRecipientInMyList(const bool inMyList);

    bool hasRecipientStack() const;
    const QString & recipientStack() const;
    void setRecipientStack(const QString & recipientString);

    bool hasRecipientSettings() const;
    const qevercloud::NotebookRecipientSettings & recipientSettings() const;
    void setNotebookRecipientSettings(
        qevercloud::NotebookRecipientSettings && settings);

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<NotebookData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::Notebook)

#endif // LIB_QUENTIER_TYPES_NOTEBOOK_H
