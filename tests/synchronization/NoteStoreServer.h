/*
 * Copyright 2023 Dmitry Ivanov
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

#pragma once

#include <qevercloud/Constants.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SyncState.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QNetworkCookie>
#include <QObject>

#include <exception>
#include <optional>
#include <variant>

QT_BEGIN_NAMESPACE

class QTcpServer;
class QTcpSocket;

QT_END_NAMESPACE

namespace quentier::synchronization::tests {

class NoteStoreServer : public QObject
{
    Q_OBJECT
public:
    struct ItemData
    {
        // Contains automatically generated or adjusted name of the item (to
        // ensure their uniqueness within the account for the items of the
        // corresponding type) if generation and/or adjustment was necessary.
        std::optional<QString> name;

        // Update sequence number assigned to the item
        qint32 usn = 0;
    };

public:
    NoteStoreServer(
        QString authenticationToken, QList<QNetworkCookie> cookies,
        QHash<qevercloud::Guid, QString> linkedNotebookAuthTokensByGuid,
        QObject * parent = nullptr);

    ~NoteStoreServer() override;

    // Saved searches
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SavedSearch>
        savedSearches() const;

    [[nodiscard]] ItemData putSavedSearch(qevercloud::SavedSearch search);
    [[nodiscard]] std::optional<qevercloud::SavedSearch> findSavedSearch(
        const QString & guid) const;

    void removeSavedSearch(const QString & guid);

    // Expunged saved searches
    void putExpungedSavedSearchGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedSavedSearchGuid(
        const QString & guid) const;

    void removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Tag> tags() const;
    [[nodiscard]] ItemData putTag(qevercloud::Tag & tag);

    [[nodiscard]] std::optional<qevercloud::Tag> findTag(
        const QString & guid) const;

    void removeTag(const QString & guid);

    // Expunged tags
    void putExpungedTagGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedTagGuid(const QString & guid) const;
    void removeExpungedTagGuid(const QString & guid);

    // Notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Notebook> notebooks()
        const;

    [[nodiscard]] ItemData putNotebook(qevercloud::Notebook notebook);
    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebook(
        const QString & guid) const;

    void removeNotebook(const QString & guid);

    [[nodiscard]] QList<qevercloud::Notebook>
        findNotebooksForLinkedNotebookGuid(
            const QString & linkedNotebookGuid) const;

    // Expunged notebooks
    void putExpungedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNotebookGuid(const QString & guid) const;
    void removeExpungedNotebookGuid(const QString & guid);

    // Notes
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Note> notes() const;
    [[nodiscard]] ItemData putNote(qevercloud::Note note);
    [[nodiscard]] std::optional<qevercloud::Note> findNote(
        const QString & guid) const;

    void removeNote(const QString & guid);

    // Expunged notes
    void putExpungedNoteGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNoteGuid(const QString & guid) const;
    void removeExpungedNoteGuid(const QString & guid);

    [[nodiscard]] QList<qevercloud::Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Resource> resources() const;
    [[nodiscard]] bool putResource(qevercloud::Resource resource);
    [[nodiscard]] std::optional<qevercloud::Resource> findResource(
        const QString & guid) const;

    void removeResource(const QString & guid);

    // Linked notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
        linkedNotebooks() const;

    [[nodiscard]] ItemData putLinkedNotebook(
        qevercloud::LinkedNotebook & linkedNotebook);

    [[nodiscard]] std::optional<qevercloud::LinkedNotebook> findLinkedNotebook(
        const QString & guid) const;

    void removeLinkedNotebook(const QString & guid);

    // Expunged linked notebooks
    void putExpungedLinkedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedLinkedNotebookGuid(
        const QString & guid) const;

    void removeExpungedLinkedNotebookGuid(const QString & guid);

    // Other
    [[nodiscard]] quint32 maxNumSavedSearches() const noexcept;
    void setMaxNumSavedSearches(quint32 maxNumSavedSearches) noexcept;

    [[nodiscard]] quint32 maxNumTags() const noexcept;
    void setMaxNumTags(quint32 maxNumTags) noexcept;

    [[nodiscard]] quint32 maxNumNotebooks() const noexcept;
    void setMaxNumNotebooks(quint32 maxNumNotebooks) noexcept;

    [[nodiscard]] quint32 maxNumNotes() const noexcept;
    void setMaxNumNotes(quint32 maxNumNotes) noexcept;

    [[nodiscard]] quint64 maxNoteSize() const noexcept;
    void setMaxNoteSize(quint64 maxNoteSize) noexcept;

    [[nodiscard]] quint32 maxNumResourcesPerNote() const noexcept;
    void setMaxNumResourcesPerNote(quint32 maxNumResourcesPerNote) noexcept;

    [[nodiscard]] quint32 maxNumTagsPerNote() const noexcept;
    void setMaxNumTagsPerNote(quint32 maxNumTagsPerNote) noexcept;

    [[nodiscard]] quint64 maxResourceSize() const noexcept;
    void setMaxResourceSize(quint64 maxResourceSize) noexcept;

private Q_SLOTS:
    void onRequestReady(const QByteArray & responseData);

private:
    [[nodiscard]] std::exception_ptr checkAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

    [[nodiscard]] std::exception_ptr checkLinkedNotetookAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

private:
    const QString m_authenticationToken;
    const QList<QNetworkCookie> m_cookies;
    const QHash<qevercloud::Guid, QString> m_linkedNotebookAuthTokensByGuid;

    QTcpServer * m_tcpServer = nullptr;
    QTcpSocket * m_tcpSocket = nullptr;
    qevercloud::NoteStoreServer * m_server = nullptr;
};

} // namespace quentier::synchronization::tests
