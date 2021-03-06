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

#ifndef LIB_QUENTIER_TYPES_SHARED_NOTEBOOK_H
#define LIB_QUENTIER_TYPES_SHARED_NOTEBOOK_H

#include <quentier/utility/Printable.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SharedNotebookData)

class QUENTIER_EXPORT SharedNotebook : public Printable
{
public:
    using SharedNotebookPrivilegeLevel =
        qevercloud::SharedNotebookPrivilegeLevel;

public:
    explicit SharedNotebook();
    SharedNotebook(const SharedNotebook & other);
    SharedNotebook(SharedNotebook && other);

    explicit SharedNotebook(
        const qevercloud::SharedNotebook & qecSharedNotebook);

    SharedNotebook & operator=(const SharedNotebook & other);
    SharedNotebook & operator=(SharedNotebook && other);

    virtual ~SharedNotebook() override;

    bool operator==(const SharedNotebook & other) const;
    bool operator!=(const SharedNotebook & other) const;

    const qevercloud::SharedNotebook & qevercloudSharedNotebook() const;
    qevercloud::SharedNotebook & qevercloudSharedNotebook();

    int indexInNotebook() const;
    void setIndexInNotebook(const int index);

    bool hasId() const;
    qint64 id() const;
    void setId(const qint64 id);

    bool hasUserId() const;
    qint32 userId() const;
    void setUserId(const qint32 userId);

    bool hasNotebookGuid() const;
    const QString & notebookGuid() const;
    void setNotebookGuid(const QString & notebookGuid);

    bool hasEmail() const;
    const QString & email() const;
    void setEmail(const QString & email);

    bool hasCreationTimestamp() const;
    qint64 creationTimestamp() const;
    void setCreationTimestamp(const qint64 timestamp);

    bool hasModificationTimestamp() const;
    qint64 modificationTimestamp() const;
    void setModificationTimestamp(const qint64 timestamp);

    bool hasUsername() const;
    const QString & username() const;
    void setUsername(const QString & username);

    bool hasPrivilegeLevel() const;
    SharedNotebookPrivilegeLevel privilegeLevel() const;
    void setPrivilegeLevel(const SharedNotebookPrivilegeLevel privilegeLevel);
    void setPrivilegeLevel(const qint8 privilegeLevel);

    bool hasReminderNotifyEmail() const;
    bool reminderNotifyEmail() const;
    void setReminderNotifyEmail(const bool notifyEmail);

    bool hasReminderNotifyApp() const;
    bool reminderNotifyApp() const;
    void setReminderNotifyApp(const bool notifyApp);

    bool hasRecipientUsername() const;
    const QString & recipientUsername() const;
    void setRecipientUsername(const QString & recipientUsername);

    bool hasRecipientUserId() const;
    qint32 recipientUserId() const;
    void setRecipientUserId(const qint32 userId);

    bool hasRecipientIdentityId() const;
    qint64 recipientIdentityId() const;
    void setRecipientIdentityId(const qint64 recipientIdentityId);

    bool hasGlobalId() const;
    const QString & globalId() const;
    void setGlobalId(const QString & globalId);

    bool hasSharerUserId() const;
    qint32 sharerUserId() const;
    void setSharerUserId(qint32 sharerUserId);

    bool hasAssignmentTimestamp() const;
    qint64 assignmentTimestamp() const;
    void setAssignmentTimestamp(const qint64 timestamp);

    virtual QTextStream & print(QTextStream & strm) const override;

    friend class Notebook;

private:
    QSharedDataPointer<SharedNotebookData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_SHARED_NOTEBOOK_H
