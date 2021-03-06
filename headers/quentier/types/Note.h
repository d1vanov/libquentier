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

#ifndef LIB_QUENTIER_TYPES_NOTE_H
#define LIB_QUENTIER_TYPES_NOTE_H

#include "IFavoritableDataElement.h"
#include "SharedNote.h"

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(NoteData)

class QUENTIER_EXPORT Note : public IFavoritableDataElement
{
public:
    QN_DECLARE_LOCAL_UID
    QN_DECLARE_DIRTY
    QN_DECLARE_FAVORITED
    QN_DECLARE_LOCAL

public:
    explicit Note();
    Note(const Note & other);
    Note(Note && other);
    Note & operator=(const Note & other);
    Note & operator=(Note && other);

    explicit Note(const qevercloud::Note & other);
    Note & operator=(const qevercloud::Note & other);

    virtual ~Note() override;

    bool operator==(const Note & other) const;
    bool operator!=(const Note & other) const;

    const qevercloud::Note & qevercloudNote() const;
    qevercloud::Note & qevercloudNote();

    virtual bool hasGuid() const override;
    virtual const QString & guid() const override;
    virtual void setGuid(const QString & guid) override;

    virtual bool hasUpdateSequenceNumber() const override;
    virtual qint32 updateSequenceNumber() const override;
    virtual void setUpdateSequenceNumber(const qint32 usn) override;

    virtual void clear() override;

    static bool validateTitle(
        const QString & title, ErrorString * pErrorDescription = nullptr);

    virtual bool checkParameters(ErrorString & errorDescription) const override;

    bool hasTitle() const;
    const QString & title() const;
    void setTitle(const QString & title);

    bool hasContent() const;
    const QString & content() const;
    void setContent(const QString & content);

    bool hasContentHash() const;
    const QByteArray & contentHash() const;
    void setContentHash(const QByteArray & contentHash);

    bool hasContentLength() const;
    qint32 contentLength() const;
    void setContentLength(const qint32 length);

    bool hasCreationTimestamp() const;
    qint64 creationTimestamp() const;
    void setCreationTimestamp(const qint64 timestamp);

    bool hasModificationTimestamp() const;
    qint64 modificationTimestamp() const;
    void setModificationTimestamp(const qint64 timestamp);

    bool hasDeletionTimestamp() const;
    qint64 deletionTimestamp() const;
    void setDeletionTimestamp(const qint64 timestamp);

    bool hasActive() const;
    bool active() const;
    void setActive(const bool active);

    bool hasNotebookGuid() const;
    const QString & notebookGuid() const;
    void setNotebookGuid(const QString & guid);

    bool hasNotebookLocalUid() const;
    const QString & notebookLocalUid() const;
    void setNotebookLocalUid(const QString & notebookLocalUid);

    bool hasTagGuids() const;
    const QStringList tagGuids() const;
    void setTagGuids(const QStringList & guids);
    void addTagGuid(const QString & guid);
    void removeTagGuid(const QString & guid);

    bool hasTagLocalUids() const;
    const QStringList & tagLocalUids() const;
    void setTagLocalUids(const QStringList & localUids);
    void addTagLocalUid(const QString & localUid);
    void removeTagLocalUid(const QString & localUid);

    bool hasResources() const;
    int numResources() const;
    QList<Resource> resources() const;
    void setResources(const QList<Resource> & resources);
    void addResource(const Resource & resource);
    bool updateResource(const Resource & resource);
    bool removeResource(const Resource & resource);

    bool hasNoteAttributes() const;
    const qevercloud::NoteAttributes & noteAttributes() const;
    qevercloud::NoteAttributes & noteAttributes();
    void clearNoteAttributes();

    bool hasSharedNotes() const;
    QList<SharedNote> sharedNotes() const;
    void setSharedNotes(const QList<SharedNote> & sharedNotes);
    void addSharedNote(const SharedNote & sharedNote);

    // NOTE: the shared note is recognized by its index in note
    // in the following two methods
    bool updateSharedNote(const SharedNote & sharedNote);
    bool removeSharedNote(const SharedNote & sharedNote);

    bool hasNoteRestrictions() const;
    const qevercloud::NoteRestrictions & noteRestrictions() const;
    qevercloud::NoteRestrictions & noteRestrictions();
    void setNoteRestrictions(qevercloud::NoteRestrictions && restrictions);

    bool hasNoteLimits() const;
    const qevercloud::NoteLimits & noteLimits() const;
    qevercloud::NoteLimits & noteLimits();
    void setNoteLimits(qevercloud::NoteLimits && limits);

    QByteArray thumbnailData() const;
    void setThumbnailData(const QByteArray & thumbnailData);

    bool isInkNote() const;

    QString plainText(ErrorString * pErrorMessage = nullptr) const;
    QStringList listOfWords(ErrorString * pErrorMessage = nullptr) const;

    std::pair<QString, QStringList> plainTextAndListOfWords(
        ErrorString * pErrorMessage = nullptr) const;

    bool containsCheckedTodo() const;
    bool containsUncheckedTodo() const;
    bool containsTodo() const;
    bool containsEncryption() const;

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<NoteData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::Note)

#endif // LIB_QUENTIER_TYPES_NOTE_H
