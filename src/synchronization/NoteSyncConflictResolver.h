/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H

#include <quentier/local_storage/LocalStorageManager.h>

#include <QObject>

#include <utility>

namespace quentier {

class INoteStore;
class LocalStorageManagerAsync;

/**
 * The NoteSyncConflictResolver class resolves the conflict between two notes:
 * the one downloaded from the remote server (but without full note data
 * downloaded yet) and the local one. The conflict resolution might lead to
 * either overriding the local conflicting notes with remote changes or to
 * clearing out Evernote-assigned fields from the local conflicting note and any
 * resources it might have - such fields as guid and update sequence number in
 * particular; in the latter case the local note would be converted to a local
 * i.e. "not yet synchronized with Evernote" note and the remote note would be
 * treated as a new note coming from Evernote
 */
class Q_DECL_HIDDEN NoteSyncConflictResolver final : public QObject
{
    Q_OBJECT
public:
    class Q_DECL_HIDDEN IManager
    {
    public:
        [[nodiscard]] virtual LocalStorageManagerAsync &
        localStorageManagerAsync() = 0;

        [[nodiscard]] virtual INoteStore * noteStoreForNote(
            const qevercloud::Note & note, QString & authToken,
            ErrorString & errorDescription) = 0;

        [[nodiscard]] virtual bool syncingLinkedNotebooksContent() const = 0;

        virtual ~IManager() = default;
    };

    explicit NoteSyncConflictResolver(
        IManager & manager, qevercloud::Note remoteNote,
        qevercloud::Note localConflict, QObject * parent = nullptr);

    void start();

    [[nodiscard]] const qevercloud::Note & remoteNote() const noexcept
    {
        return m_remoteNote;
    }

    [[nodiscard]] const qevercloud::Note & localConflict() const noexcept
    {
        return m_localConflict;
    }

public Q_SLOTS:
    void onAuthDataUpdated(
        QString authToken, QString shardId,
        qevercloud::Timestamp expirationTime);

    void onLinkedNotebooksAuthDataUpdated(
        QHash<QString, std::pair<QString, QString>>
            authTokensAndShardIdsByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp>
            authTokenExpirationTimesByLinkedNotebookGuid);

Q_SIGNALS:
    void finished(qevercloud::Note remoteNote);
    void failure(qevercloud::Note remoteNote, ErrorString errorDescription);

    void rateLimitExceeded(qint32 secondsToWait);
    void notifyAuthExpiration();

    // private signals
    void addNote(qevercloud::Note note, QUuid requestId);

    void updateNote(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

private Q_SLOTS:
    void onAddNoteComplete(qevercloud::Note note, QUuid requestId);

    void onAddNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    void onUpdateNoteComplete(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        QUuid requestId);

    void onUpdateNoteFailed(
        qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onGetNoteAsyncFinished(
        qint32 errorCode, qevercloud::Note qecNote, qint32 rateLimitSeconds,
        ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processNotesConflictByGuid();
    void overrideLocalNoteWithRemoteChanges();
    void addRemoteNoteToLocalStorageAsNewNote();
    bool downloadFullRemoteNoteData();

private:
    void timerEvent(QTimerEvent * pEvent) override;

private:
    Q_DISABLE_COPY(NoteSyncConflictResolver)

private:
    IManager & m_manager;

    qevercloud::Note m_remoteNote;
    qevercloud::Note m_localConflict;

    qevercloud::Note m_remoteNoteAsLocalNote;

    bool m_shouldOverrideLocalNoteWithRemoteNote = false;

    bool m_pendingLocalConflictUpdateInLocalStorage = false;
    bool m_pendingFullRemoteNoteDataDownload = false;
    bool m_pendingRemoteNoteAdditionToLocalStorage = false;
    bool m_pendingRemoteNoteUpdateInLocalStorage = false;

    bool m_pendingAuthDataUpdate = false;
    bool m_pendingLinkedNotebookAuthDataUpdate = false;

    int m_retryNoteDownloadingTimerId = 0;

    QUuid m_addNoteRequestId;
    QUuid m_updateNoteRequestId;

    bool m_started = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_SYNC_CONFLICT_RESOLVER_H
