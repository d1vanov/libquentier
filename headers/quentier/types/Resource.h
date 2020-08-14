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

#ifndef LIB_QUENTIER_TYPES_RESOURCE_H
#define LIB_QUENTIER_TYPES_RESOURCE_H

#include "INoteStoreDataElement.h"
#include "Note.h"

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceData)

class QUENTIER_EXPORT Resource : public INoteStoreDataElement
{
public:
    QN_DECLARE_LOCAL_UID
    QN_DECLARE_DIRTY
    QN_DECLARE_LOCAL

public:
    explicit Resource();
    Resource(const Resource & other);
    Resource(Resource && other);
    explicit Resource(const qevercloud::Resource & resource);
    Resource & operator=(const Resource & other);
    Resource & operator=(Resource && other);
    virtual ~Resource() override;

    bool operator==(const Resource & other) const;
    bool operator!=(const Resource & other) const;

    const qevercloud::Resource & qevercloudResource() const;
    qevercloud::Resource & qevercloudResource();

    virtual void clear() override;

    virtual bool hasGuid() const override;
    virtual const QString & guid() const override;
    virtual void setGuid(const QString & guid) override;

    virtual bool hasUpdateSequenceNumber() const override;
    virtual qint32 updateSequenceNumber() const override;

    virtual void setUpdateSequenceNumber(
        const qint32 updateSequenceNumber) override;

    virtual bool checkParameters(ErrorString & errorDescription) const override;

    QString displayName() const;
    void setDisplayName(const QString & displayName);

    QString preferredFileSuffix() const;

    int indexInNote() const;
    void setIndexInNote(const int index);

    bool hasNoteGuid() const;
    const QString & noteGuid() const;
    void setNoteGuid(const QString & guid);

    bool hasNoteLocalUid() const;
    const QString & noteLocalUid() const;
    void setNoteLocalUid(const QString & guid);

    bool hasData() const;

    bool hasDataHash() const;
    const QByteArray & dataHash() const;
    void setDataHash(const QByteArray & hash);

    bool hasDataSize() const;
    qint32 dataSize() const;
    void setDataSize(const qint32 size);

    bool hasDataBody() const;
    const QByteArray & dataBody() const;
    void setDataBody(const QByteArray & body);

    bool hasMime() const;
    const QString & mime() const;
    void setMime(const QString & mime);

    bool hasWidth() const;
    qint16 width() const;
    void setWidth(const qint16 width);

    bool hasHeight() const;
    qint16 height() const;
    void setHeight(const qint16 height);

    bool hasRecognitionData() const;

    bool hasRecognitionDataHash() const;
    const QByteArray & recognitionDataHash() const;
    void setRecognitionDataHash(const QByteArray & hash);

    bool hasRecognitionDataSize() const;
    qint32 recognitionDataSize() const;
    void setRecognitionDataSize(const qint32 size);

    bool hasRecognitionDataBody() const;
    const QByteArray & recognitionDataBody() const;
    void setRecognitionDataBody(const QByteArray & body);

    bool hasAlternateData() const;

    bool hasAlternateDataHash() const;
    const QByteArray & alternateDataHash() const;
    void setAlternateDataHash(const QByteArray & hash);

    bool hasAlternateDataSize() const;
    qint32 alternateDataSize() const;
    void setAlternateDataSize(const qint32 size);

    bool hasAlternateDataBody() const;
    const QByteArray & alternateDataBody() const;
    void setAlternateDataBody(const QByteArray & body);

    bool hasResourceAttributes() const;
    const qevercloud::ResourceAttributes & resourceAttributes() const;
    qevercloud::ResourceAttributes & resourceAttributes();

    void setResourceAttributes(
        const qevercloud::ResourceAttributes & attributes);

    void setResourceAttributes(qevercloud::ResourceAttributes && attributes);

    friend class Note;

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<ResourceData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_RESOURCE_H
