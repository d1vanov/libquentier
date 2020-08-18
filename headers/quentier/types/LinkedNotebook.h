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

#ifndef LIB_QUENTIER_TYPES_LINKED_NOTEBOOK_H
#define LIB_QUENTIER_TYPES_LINKED_NOTEBOOK_H

#include "INoteStoreDataElement.h"

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LinkedNotebookData)

class QUENTIER_EXPORT LinkedNotebook : public INoteStoreDataElement
{
public:
    QN_DECLARE_DIRTY

public:
    explicit LinkedNotebook();
    LinkedNotebook(const LinkedNotebook & other);
    LinkedNotebook(LinkedNotebook && other);
    LinkedNotebook & operator=(const LinkedNotebook & other);
    LinkedNotebook & operator=(LinkedNotebook && other);

    explicit LinkedNotebook(const qevercloud::LinkedNotebook & linkedNotebook);
    explicit LinkedNotebook(qevercloud::LinkedNotebook && linkedNotebook);

    virtual ~LinkedNotebook() override;

    const qevercloud::LinkedNotebook & qevercloudLinkedNotebook() const;
    qevercloud::LinkedNotebook & qevercloudLinkedNotebook();

    bool operator==(const LinkedNotebook & other) const;
    bool operator!=(const LinkedNotebook & other) const;

    virtual void clear() override;

    virtual bool hasGuid() const override;
    virtual const QString & guid() const override;
    virtual void setGuid(const QString & guid) override;

    virtual bool hasUpdateSequenceNumber() const override;
    virtual qint32 updateSequenceNumber() const override;
    virtual void setUpdateSequenceNumber(const qint32 usn) override;

    virtual bool checkParameters(ErrorString & errorDescription) const override;

    bool hasShareName() const;
    const QString & shareName() const;
    void setShareName(const QString & shareName);

    bool hasUsername() const;
    const QString & username() const;
    void setUsername(const QString & username);

    bool hasShardId() const;
    const QString & shardId() const;
    void setShardId(const QString & shardId);

    bool hasSharedNotebookGlobalId() const;
    const QString & sharedNotebookGlobalId() const;
    void setSharedNotebookGlobalId(const QString & sharedNotebookGlobalId);

    bool hasUri() const;
    const QString & uri() const;
    void setUri(const QString & uri);

    bool hasNoteStoreUrl() const;
    const QString & noteStoreUrl() const;
    void setNoteStoreUrl(const QString & noteStoreUrl);

    bool hasWebApiUrlPrefix() const;
    const QString & webApiUrlPrefix() const;
    void setWebApiUrlPrefix(const QString & webApiUrlPrefix);

    bool hasStack() const;
    const QString & stack() const;
    void setStack(const QString & stack);

    bool hasBusinessId() const;
    qint32 businessId() const;
    void setBusinessId(const qint32 businessId);

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    // hide useless methods inherited from the base class from public interface
    virtual const QString localUid() const override
    {
        return QString();
    }
    virtual void setLocalUid(const QString &) override {}
    virtual void unsetLocalUid() override {}

    virtual bool isLocal() const override
    {
        return false;
    }
    virtual void setLocal(const bool) override {}

private:
    QSharedDataPointer<LinkedNotebookData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::LinkedNotebook)

#endif // LIB_QUENTIER_TYPES_LINKED_NOTEBOOK_H
