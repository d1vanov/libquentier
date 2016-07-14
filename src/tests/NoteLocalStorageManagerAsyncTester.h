#ifndef LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/utility/Qt4Helper.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerThreadWorker)

namespace test {

class NoteLocalStorageManagerAsyncTester : public QObject
{
    Q_OBJECT
public:
    explicit NoteLocalStorageManagerAsyncTester(QObject * parent = Q_NULLPTR);
    ~NoteLocalStorageManagerAsyncTester();

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

// private signals
    void addNotebookRequest(Notebook notebook, QUuid requestId = QUuid());
    void getNoteCountRequest(QUuid requestId = QUuid());
    void addNoteRequest(Note note, QUuid requestId = QUuid());
    void updateNoteRequest(Note note, bool updateResources, bool updateTags, QUuid requestId = QUuid());
    void findNoteRequest(Note note, bool withResourceBinaryData, QUuid requestId = QUuid());
    void listNotesPerNotebookRequest(Notebook notebook, bool withResourceBinaryData,
                                     LocalStorageManager::ListObjectsOptions flag,
                                     size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                     LocalStorageManager::OrderDirection::type orderDirection,
                                     QUuid requestId = QUuid());
    void expungeNoteRequest(Note note, QUuid requestId = QUuid());

private Q_SLOTS:
    void onWorkerInitialized();
    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);
    void onAddNotebookFailed(Notebook notebook, QNLocalizedString errorDescription, QUuid requestId);
    void onNoteCountCompleted(int count, QUuid requestId);
    void onNoteCountFailed(QNLocalizedString errorDescription, QUuid requestId);
    void onAddNoteCompleted(Note note, QUuid requestId);
    void onAddNoteFailed(Note note, QNLocalizedString errorDescription, QUuid requestId);
    void onUpdateNoteCompleted(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                            QNLocalizedString errorDescription, QUuid requestId);
    void onFindNoteCompleted(Note note, bool withResourceBinaryData, QUuid requestId);
    void onFindNoteFailed(Note note, bool withResourceBinaryData, QNLocalizedString errorDescription, QUuid requestId);
    void onListNotesPerNotebookCompleted(Notebook notebook, bool withResourceBinaryData,
                                         LocalStorageManager::ListObjectsOptions flag,
                                         size_t limit, size_t offset,
                                         LocalStorageManager::ListNotesOrder::type order,
                                         LocalStorageManager::OrderDirection::type orderDirection,
                                         QList<Note> notes, QUuid requestId);
    void onListNotesPerNotebookFailed(Notebook notebook, bool withResourceBinaryData,
                                      LocalStorageManager::ListObjectsOptions flag,
                                      size_t limit, size_t offset,
                                      LocalStorageManager::ListNotesOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QNLocalizedString errorDescription, QUuid requestId);
    void onExpungeNoteCompleted(Note note, QUuid requestId);
    void onExpungeNoteFailed(Note note, QNLocalizedString errorDescription, QUuid requestId);

    void onFailure(QNLocalizedString errorDescription);

private:
    void createConnections();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_DELETE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_TWO_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTE_THREE_REQUEST,
        STATE_SENT_LIST_NOTES_PER_NOTEBOOK_ONE_REQUEST,
        STATE_SENT_LIST_NOTES_PER_NOTEBOOK_TWO_REQUEST
    };

    State m_state;

    LocalStorageManagerThreadWorker *   m_pLocalStorageManagerThreadWorker;
    QThread *                           m_pLocalStorageManagerThread;

    Notebook                    m_notebook;
    Notebook                    m_extraNotebook;
    Note                        m_initialNote;
    Note                        m_foundNote;
    Note                        m_modifiedNote;
    QList<Note>                 m_initialNotes;
    QList<Note>                 m_extraNotes;
};

} // namespace quentier
} // namespace test

#endif // LIB_QUENTIER_TESTS_NOTE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
