#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_THREAD_WORKER_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_THREAD_WORKER_H

#include <quentier/utility/Qt4Helper.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/local_storage/LocalStorageCacheManager.h>
#include <quentier/types/UserWrapper.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SharedNotebookWrapper.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Tag.h>
#include <quentier/types/ResourceWrapper.h>
#include <quentier/types/SavedSearch.h>
#include <QObject>

namespace quentier {

class QUENTIER_EXPORT LocalStorageManagerThreadWorker: public QObject
{
    Q_OBJECT
public:
    explicit LocalStorageManagerThreadWorker(const QString & username, const qint32 userId,
                                             const bool startFromScratch, const bool overrideLock,
                                             QObject * parent = Q_NULLPTR);
    virtual ~LocalStorageManagerThreadWorker();

    void setUseCache(const bool useCache);

    const LocalStorageCacheManager * localStorageCacheManager() const;

Q_SIGNALS:
    // Generic failure signal
    void failure(QString errorDescription);

    // Sent when the initialization is complete
    void initialized();

    // User-related signals:
    void getUserCountComplete(int userCount, QUuid requestId = QUuid());
    void getUserCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void switchUserComplete(qint32 userId, QUuid requestId = QUuid());
    void switchUserFailed(qint32 userId, QString errorDescription, QUuid requestId = QUuid());
    void addUserComplete(UserWrapper user, QUuid requestId = QUuid());
    void addUserFailed(UserWrapper user, QString errorDescription, QUuid requestId = QUuid());
    void updateUserComplete(UserWrapper user, QUuid requestId = QUuid());
    void updateUserFailed(UserWrapper user, QString errorDescription, QUuid requestId = QUuid());
    void findUserComplete(UserWrapper foundUser, QUuid requestId = QUuid());
    void findUserFailed(UserWrapper user, QString errorDescription, QUuid requestId = QUuid());
    void deleteUserComplete(UserWrapper user, QUuid requestId = QUuid());
    void deleteUserFailed(UserWrapper user, QString errorDescription, QUuid requestId = QUuid());
    void expungeUserComplete(UserWrapper user, QUuid requestId = QUuid());
    void expungeUserFailed(UserWrapper user, QString errorDescription, QUuid requestId = QUuid());

    // Notebook-related signals:
    void getNotebookCountComplete(int notebookCount, QUuid requestId = QUuid());
    void getNotebookCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void addNotebookComplete(Notebook notebook, QUuid requestId = QUuid());
    void addNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void updateNotebookComplete(Notebook notebook, QUuid requestId = QUuid());
    void updateNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void findNotebookComplete(Notebook foundNotebook, QUuid requestId = QUuid());
    void findNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void findDefaultNotebookComplete(Notebook foundNotebook, QUuid requestId = QUuid());
    void findDefaultNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void findLastUsedNotebookComplete(Notebook foundNotebook, QUuid requestId = QUuid());
    void findLastUsedNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void findDefaultOrLastUsedNotebookComplete(Notebook foundNotebook, QUuid requestId = QUuid());
    void findDefaultOrLastUsedNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());
    void listAllNotebooksComplete(size_t limit, size_t offset, LocalStorageManager::ListNotebooksOrder::type order,
                                  LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                                  QList<Notebook> foundNotebooks, QUuid requestId = QUuid());
    void listAllNotebooksFailed(size_t limit, size_t offset, LocalStorageManager::ListNotebooksOrder::type order,
                                LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                                QString errorDescription, QUuid requestId = QUuid());
    void listNotebooksComplete(LocalStorageManager::ListObjectsOptions flag,
                               size_t limit, size_t offset, LocalStorageManager::ListNotebooksOrder::type order,
                               LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                               QList<Notebook> foundNotebooks, QUuid requestId = QUuid());
    void listNotebooksFailed(LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
                             LocalStorageManager::ListNotebooksOrder::type order,
                             LocalStorageManager::OrderDirection::type orderDirection,
                             QString linkedNotebookGuid, QString errorDescription, QUuid requestId = QUuid());
    void listAllSharedNotebooksComplete(QList<SharedNotebookWrapper> foundSharedNotebooks, QUuid requestId = QUuid());
    void listAllSharedNotebooksFailed(QString errorDescription, QUuid requestId = QUuid());
    void listSharedNotebooksPerNotebookGuidComplete(QString notebookGuid, QList<SharedNotebookWrapper> foundSharedNotebooks,
                                                    QUuid requestId = QUuid());
    void listSharedNotebooksPerNotebookGuidFailed(QString notebookGuid, QString errorDescription, QUuid requestId = QUuid());
    void expungeNotebookComplete(Notebook notebook, QUuid requestId = QUuid());
    void expungeNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId = QUuid());

    // Linked notebook-related signals:
    void getLinkedNotebookCountComplete(int linkedNotebookCount, QUuid requestId = QUuid());
    void getLinkedNotebookCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void addLinkedNotebookComplete(LinkedNotebook linkedNotebook, QUuid requestId = QUuid());
    void addLinkedNotebookFailed(LinkedNotebook linkedNotebook, QString errorDescription, QUuid requestId = QUuid());
    void updateLinkedNotebookComplete(LinkedNotebook linkedNotebook, QUuid requestId = QUuid());
    void updateLinkedNotebookFailed(LinkedNotebook linkedNotebook, QString errorDescription, QUuid requestId = QUuid());
    void findLinkedNotebookComplete(LinkedNotebook foundLinkedNotebook, QUuid requestId = QUuid());
    void findLinkedNotebookFailed(LinkedNotebook linkedNotebook, QString errorDescription, QUuid requestId = QUuid());
    void listAllLinkedNotebooksComplete(size_t limit, size_t offset,
                                        LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                        LocalStorageManager::OrderDirection::type orderDirection,
                                        QList<LinkedNotebook> foundLinkedNotebooks,
                                        QUuid requestId = QUuid());
    void listAllLinkedNotebooksFailed(size_t limit, size_t offset,
                                      LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QString errorDescription, QUuid requestId = QUuid());
    void listLinkedNotebooksComplete(LocalStorageManager::ListObjectsOptions flag,
                                     size_t limit, size_t offset,
                                     LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                     LocalStorageManager::OrderDirection::type orderDirection,
                                     QList<LinkedNotebook> foundLinkedNotebooks,
                                     QUuid requestId = QUuid());
    void listLinkedNotebooksFailed(LocalStorageManager::ListObjectsOptions flag,
                                   size_t limit, size_t offset,
                                   LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                   LocalStorageManager::OrderDirection::type orderDirection,
                                   QString errorDescription, QUuid requestId = QUuid());
    void expungeLinkedNotebookComplete(LinkedNotebook linkedNotebook, QUuid requestId = QUuid());
    void expungeLinkedNotebookFailed(LinkedNotebook linkedNotebook, QString errorDescription, QUuid requestId = QUuid());

    // Note-related signals:
    void noteCountComplete(int noteCount, QUuid requestId = QUuid());
    void noteCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void noteCountPerNotebookComplete(int noteCount, Notebook notebook, QUuid requestId = QUuid());
    void noteCountPerNotebookFailed(QString errorDescription, Notebook notebook, QUuid requestId = QUuid());
    void noteCountPerTagComplete(int noteCount, Tag tag, QUuid requestId = QUuid());
    void noteCountPerTagFailed(QString errorDescription, Tag tag, QUuid requestId = QUuid());
    void addNoteComplete(Note note, QUuid requestId = QUuid());
    void addNoteFailed(Note note, QString errorDescription, QUuid requestId = QUuid());
    void updateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId = QUuid());
    void updateNoteFailed(Note note, bool updateResources, bool updateTags,
                          QString errorDescription, QUuid requestId = QUuid());
    void findNoteComplete(Note foundNote, bool withResourceBinaryData, QUuid requestId = QUuid());
    void findNoteFailed(Note note, bool withResourceBinaryData, QString errorDescription, QUuid requestId = QUuid());
    void listNotesPerNotebookComplete(Notebook notebook, bool withResourceBinaryData,
                                      LocalStorageManager::ListObjectsOptions flag,
                                      size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QList<Note> foundNotes, QUuid requestId = QUuid());
    void listNotesPerNotebookFailed(Notebook notebook, bool withResourceBinaryData,
                                    LocalStorageManager::ListObjectsOptions flag,
                                    size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                    LocalStorageManager::OrderDirection::type orderDirection,
                                    QString errorDescription, QUuid requestId = QUuid());
    void listNotesPerTagComplete(Tag tag, bool withResourceBinaryData,
                                 LocalStorageManager::ListObjectsOptions flag,
                                 size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                                 LocalStorageManager::OrderDirection::type orderDirection,
                                 QList<Note> foundNotes, QUuid requestId = QUuid());
    void listNotesPerTagFailed(Tag tag, bool withResourceBinaryData,
                               LocalStorageManager::ListObjectsOptions flag,
                               size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                               LocalStorageManager::OrderDirection::type orderDirection,
                               QString errorDescription, QUuid requestId = QUuid());
    void listNotesComplete(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                           size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                           LocalStorageManager::OrderDirection::type orderDirection,
                           QList<Note> foundNotes, QUuid requestId = QUuid());
    void listNotesFailed(LocalStorageManager::ListObjectsOptions flag, bool withResourceBinaryData,
                         size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
                         LocalStorageManager::OrderDirection::type orderDirection,
                         QString errorDescription, QUuid requestId = QUuid());
    void expungeNoteComplete(Note note, QUuid requestId = QUuid());
    void expungeNoteFailed(Note note, QString errorDescription, QUuid requestId = QUuid());

    // Tag-related signals:
    void getTagCountComplete(int tagCount, QUuid requestId = QUuid());
    void getTagCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void addTagComplete(Tag tag, QUuid requestId = QUuid());
    void addTagFailed(Tag tag, QString errorDescription, QUuid requestId = QUuid());
    void updateTagComplete(Tag tag, QUuid requestId = QUuid());
    void updateTagFailed(Tag tag, QString errorDescription, QUuid requestId = QUuid());
    void linkTagWithNoteComplete(Tag tag, Note note, QUuid requestId = QUuid());
    void linkTagWithNoteFailed(Tag tag, Note note, QString errorDescription, QUuid requestId = QUuid());
    void findTagComplete(Tag tag, QUuid requestId = QUuid());
    void findTagFailed(Tag tag, QString errorDescription, QUuid requestId = QUuid());
    void listAllTagsPerNoteComplete(QList<Tag> foundTags, Note note,
                                    LocalStorageManager::ListObjectsOptions flag,
                                    size_t limit, size_t offset,
                                    LocalStorageManager::ListTagsOrder::type order,
                                    LocalStorageManager::OrderDirection::type orderDirection,
                                    QUuid requestId = QUuid());
    void listAllTagsPerNoteFailed(Note note, LocalStorageManager::ListObjectsOptions flag,
                                  size_t limit, size_t offset,
                                  LocalStorageManager::ListTagsOrder::type order,
                                  LocalStorageManager::OrderDirection::type orderDirection,
                                  QString errorDescription, QUuid requestId = QUuid());
    void listAllTagsComplete(size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                             LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                             QList<Tag> foundTags, QUuid requestId = QUuid());
    void listAllTagsFailed(size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                           LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                           QString errorDescription, QUuid requestId = QUuid());
    void listTagsComplete(LocalStorageManager::ListObjectsOptions flag,
                          size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                          LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                          QList<Tag> foundTags, QUuid requestId = QUuid());
    void listTagsFailed(LocalStorageManager::ListObjectsOptions flag,
                        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                        LocalStorageManager::OrderDirection::type orderDirection, QString linkedNotebookGuid,
                        QString errorDescription, QUuid requestId = QUuid());
    void expungeTagComplete(Tag tag, QUuid requestId = QUuid());
    void expungeTagFailed(Tag tag, QString errorDescription, QUuid requestId = QUuid());
    void expungeNotelessTagsFromLinkedNotebooksComplete(QUuid requestId = QUuid());
    void expungeNotelessTagsFromLinkedNotebooksFailed(QString errorDescription, QUuid requestId = QUuid());

    // Resource-related signals:
    void getResourceCountComplete(int resourceCount, QUuid requestId = QUuid());
    void getResourceCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void addResourceComplete(ResourceWrapper resource, QUuid requestId = QUuid());
    void addResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId = QUuid());
    void updateResourceComplete(ResourceWrapper resource, QUuid requestId = QUuid());
    void updateResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId = QUuid());
    void findResourceComplete(ResourceWrapper resource, bool withBinaryData, QUuid requestId = QUuid());
    void findResourceFailed(ResourceWrapper resource, bool withBinaryData, QString errorDescription, QUuid requestId = QUuid());
    void expungeResourceComplete(ResourceWrapper resource, QUuid requestId = QUuid());
    void expungeResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId = QUuid());

    // Saved search-related signals:
    void getSavedSearchCountComplete(int savedSearchCount, QUuid requestId = QUuid());
    void getSavedSearchCountFailed(QString errorDescription, QUuid requestId = QUuid());
    void addSavedSearchComplete(SavedSearch search, QUuid requestId = QUuid());
    void addSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId = QUuid());
    void updateSavedSearchComplete(SavedSearch search, QUuid requestId = QUuid());
    void updateSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId = QUuid());
    void findSavedSearchComplete(SavedSearch search, QUuid requestId = QUuid());
    void findSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId = QUuid());
    void listAllSavedSearchesComplete(size_t limit, size_t offset,
                                      LocalStorageManager::ListSavedSearchesOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QList<SavedSearch> foundSearches, QUuid requestId = QUuid());
    void listAllSavedSearchesFailed(size_t limit, size_t offset,
                                    LocalStorageManager::ListSavedSearchesOrder::type order,
                                    LocalStorageManager::OrderDirection::type orderDirection,
                                    QString errorDescription, QUuid requestId = QUuid());
    void listSavedSearchesComplete(LocalStorageManager::ListObjectsOptions flag,
                                   size_t limit, size_t offset,
                                   LocalStorageManager::ListSavedSearchesOrder::type order,
                                   LocalStorageManager::OrderDirection::type orderDirection,
                                   QList<SavedSearch> foundSearches, QUuid requestId = QUuid());
    void listSavedSearchesFailed(LocalStorageManager::ListObjectsOptions flag,
                                 size_t limit, size_t offset,
                                 LocalStorageManager::ListSavedSearchesOrder::type order,
                                 LocalStorageManager::OrderDirection::type orderDirection,
                                 QString errorDescription, QUuid requestId = QUuid());
    void expungeSavedSearchComplete(SavedSearch search, QUuid requestId = QUuid());
    void expungeSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId = QUuid());

public Q_SLOTS:
    void init();

    // User-related slots:
    void onGetUserCountRequest(QUuid requestId);
    void onSwitchUserRequest(QString username, qint32 userId, bool startFromScratch, QUuid requestId);
    void onAddUserRequest(UserWrapper user, QUuid requestId);
    void onUpdateUserRequest(UserWrapper user, QUuid requestId);
    void onFindUserRequest(UserWrapper user, QUuid requestId);
    void onDeleteUserRequest(UserWrapper user, QUuid requestId);
    void onExpungeUserRequest(UserWrapper user, QUuid requestId);

    // Notebook-related slots:
    void onGetNotebookCountRequest(QUuid requestId);
    void onAddNotebookRequest(Notebook notebook, QUuid requestId);
    void onUpdateNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindDefaultNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindLastUsedNotebookRequest(Notebook notebook, QUuid requestId);
    void onFindDefaultOrLastUsedNotebookRequest(Notebook notebook, QUuid requestId);
    void onListAllNotebooksRequest(size_t limit, size_t offset,
                                   LocalStorageManager::ListNotebooksOrder::type order,
                                   LocalStorageManager::OrderDirection::type orderDirection,
                                   QString linkedNotebookGuid, QUuid requestId);
    void onListAllSharedNotebooksRequest(QUuid requestId);
    void onListNotebooksRequest(LocalStorageManager::ListObjectsOptions flag,
                                size_t limit, size_t offset,
                                LocalStorageManager::ListNotebooksOrder::type order,
                                LocalStorageManager::OrderDirection::type orderDirection,
                                QString linkedNotebookGuid, QUuid requestId);
    void onListSharedNotebooksPerNotebookGuidRequest(QString notebookGuid, QUuid requestId);
    void onExpungeNotebookRequest(Notebook notebook, QUuid requestId);

    // Linked notebook-related slots:
    void onGetLinkedNotebookCountRequest(QUuid requestId);
    void onAddLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId);
    void onUpdateLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId);
    void onFindLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId);
    void onListAllLinkedNotebooksRequest(size_t limit, size_t offset,
                                         LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                         LocalStorageManager::OrderDirection::type orderDirection,
                                         QUuid requestId);
    void onListLinkedNotebooksRequest(LocalStorageManager::ListObjectsOptions flag,
                                      size_t limit, size_t offset,
                                      LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QUuid requestId);
    void onExpungeLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId);

    // Note-related slots:
    void onNoteCountRequest(QUuid requestId);
    void onNoteCountPerNotebookRequest(Notebook notebook, QUuid requestId);
    void onNoteCountPerTagRequest(Tag tag, QUuid requestId);
    void onAddNoteRequest(Note note, QUuid requestId);
    void onUpdateNoteRequest(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void onFindNoteRequest(Note note, bool withResourceBinaryData, QUuid requestId);
    void onListNotesPerNotebookRequest(Notebook notebook, bool withResourceBinaryData,
                                       LocalStorageManager::ListObjectsOptions flag,
                                       size_t limit, size_t offset,
                                       LocalStorageManager::ListNotesOrder::type order,
                                       LocalStorageManager::OrderDirection::type orderDirection,
                                       QUuid requestId);
    void onListNotesPerTagRequest(Tag tag, bool withResourceBinaryData,
                                  LocalStorageManager::ListObjectsOptions flag,
                                  size_t limit, size_t offset,
                                  LocalStorageManager::ListNotesOrder::type order,
                                  LocalStorageManager::OrderDirection::type orderDirection,
                                  QUuid requestId);
    void onListNotesRequest(LocalStorageManager::ListObjectsOptions flag,
                            bool withResourceBinaryData, size_t limit, size_t offset,
                            LocalStorageManager::ListNotesOrder::type order,
                            LocalStorageManager::OrderDirection::type orderDirection,
                            QUuid requestId);
    void onExpungeNoteRequest(Note note, QUuid requestId);

    // Tag-related slots:
    void onGetTagCountRequest(QUuid requestId);
    void onAddTagRequest(Tag tag, QUuid requestId);
    void onUpdateTagRequest(Tag tag, QUuid requestId);
    void onFindTagRequest(Tag tag, QUuid requestId);
    void onListAllTagsPerNoteRequest(Note note, LocalStorageManager::ListObjectsOptions flag,
                                     size_t limit, size_t offset,
                                     LocalStorageManager::ListTagsOrder::type order,
                                     LocalStorageManager::OrderDirection::type orderDirection,
                                     QUuid requestId);
    void onListAllTagsRequest(size_t limit, size_t offset,
                              LocalStorageManager::ListTagsOrder::type order,
                              LocalStorageManager::OrderDirection::type orderDirection,
                              QString linkedNotebookGuid, QUuid requestId);
    void onListTagsRequest(LocalStorageManager::ListObjectsOptions flag,
                           size_t limit, size_t offset,
                           LocalStorageManager::ListTagsOrder::type order,
                           LocalStorageManager::OrderDirection::type orderDirection,
                           QString linkedNotebookGuid, QUuid requestId);
    void onExpungeTagRequest(Tag tag, QUuid requestId);
    void onExpungeNotelessTagsFromLinkedNotebooksRequest(QUuid requestId);

    // Resource-related slots:
    void onGetResourceCountRequest(QUuid requestId);
    void onAddResourceRequest(ResourceWrapper resource, QUuid requestId);
    void onUpdateResourceRequest(ResourceWrapper resource, QUuid requestId);
    void onFindResourceRequest(ResourceWrapper resource, bool withBinaryData, QUuid requestId);
    void onExpungeResourceRequest(ResourceWrapper resource, QUuid requestId);

    // Saved search-related slots:
    void onGetSavedSearchCountRequest(QUuid requestId);
    void onAddSavedSearchRequest(SavedSearch search, QUuid requestId);
    void onUpdateSavedSearchRequest(SavedSearch search, QUuid requestId);
    void onFindSavedSearchRequest(SavedSearch search, QUuid requestId);
    void onListAllSavedSearchesRequest(size_t limit, size_t offset,
                                       LocalStorageManager::ListSavedSearchesOrder::type order,
                                       LocalStorageManager::OrderDirection::type orderDirection,
                                       QUuid requestId);
    void onListSavedSearchesRequest(LocalStorageManager::ListObjectsOptions flag,
                                    size_t limit, size_t offset,
                                    LocalStorageManager::ListSavedSearchesOrder::type order,
                                    LocalStorageManager::OrderDirection::type orderDirection,
                                    QUuid requestId);
    void onExpungeSavedSearch(SavedSearch search, QUuid requestId);

private:
    LocalStorageManagerThreadWorker() Q_DECL_DELETE;
    Q_DISABLE_COPY(LocalStorageManagerThreadWorker)

    QString                     m_username;
    qint32                      m_userId;
    bool                        m_startFromScratch;
    bool                        m_overrideLock;
    LocalStorageManager *       m_pLocalStorageManager;
    bool                        m_useCache;
    LocalStorageCacheManager *  m_pLocalStorageCacheManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_THREAD_WORKER_H
