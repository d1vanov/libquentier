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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_H

#include <quentier/local_storage/Lists.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>
#include <memory>
#include <utility>

namespace qevercloud {

QT_FORWARD_DECLARE_STRUCT(Accounting)
QT_FORWARD_DECLARE_STRUCT(BusinessUserInfo)
QT_FORWARD_DECLARE_STRUCT(NoteAttributes)
QT_FORWARD_DECLARE_STRUCT(NotebookRestrictions)
QT_FORWARD_DECLARE_STRUCT(ResourceAttributes)
QT_FORWARD_DECLARE_STRUCT(PremiumInfo)
QT_FORWARD_DECLARE_STRUCT(SharedNotebook)
QT_FORWARD_DECLARE_STRUCT(UserAttributes)

} // namespace qevercloud

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ILocalStoragePatch)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

class QUENTIER_EXPORT LocalStorageManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief The StartupOption enum is a QFlags enum which allows to specify
     * some options to be applied to the local storage database on startup or
     * on call to switchUser method.
     */
    enum class StartupOption
    {
        /**
         * If ClearDatabase flag is active, LocalStorageManager
         * would wipe any existing database contents; the net effect
         * would be as if no database existed for the given user before
         * the creation of LocalStorageManager or before the call to
         * its switchUser method
         */
        ClearDatabase = 1,
        /**
         * If OverrideLock flag is active, LocalStorageManager would ignore
         * the existing advisory lock (if any) put on the database file;
         * if this flag is not active, the attempt to create
         * LocalStorageManager (or the attempt to call its switchUser
         * method) with the advisory lock on the database file put by
         * someone else would cause the throwing of DatabaseLockedException
         */
        OverrideLock = 2
    };
    Q_DECLARE_FLAGS(StartupOptions, StartupOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOptions options);

    /**
     * @brief LocalStorageManager - constructor. Takes in the account for which
     * the LocalStorageManager instance is created plus some other parameters
     * determining the startup behaviour
     *
     * @param account           The account for which the local storage is being
     *                          created and initialized
     * @param options           Startup options for the local storage, none
     *                          enabled by default
     * @param parent            Parent QObject
     */
    explicit LocalStorageManager(
        const Account & account,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        const StartupOptions options = {},
#else
        const StartupOptions options = 0,
#endif
        QObject * parent = nullptr);

    virtual ~LocalStorageManager() override;

Q_SIGNALS:
    /**
     * @brief LocalStorageManager is capable of performing automatic database
     * upgrades if/when it is necessary
     *
     * As the database upgrade can be a lengthy operation, this signal is meant
     * to provide some feedback on the progress of the upgrade
     *
     * @param progress      The value from 0 to 1 denoting the database upgrade
     *                      progress
     */
    void upgradeProgress(double progress);

public:
    /**
     * @brief The ListObjectsOption enum is a QFlags enum which allows to
     * specify the desired local storage elements in calls to methods listing
     * them from the database.
     *
     * For example, one can either list all available elements of certain type
     * from local storage or only elements marked as dirty (modified locally,
     * not yet synchronized) or elements never synchronized with the remote
     * storage or elements which are synchronizable with the remote storage etc.
     */
    enum class ListObjectsOption
    {
        ListAll = 0,
        ListDirty = 1,
        ListNonDirty = 2,
        ListElementsWithoutGuid = 4,
        ListElementsWithGuid = 8,
        ListLocal = 16,
        ListNonLocal = 32,
        ListFavoritedElements = 64,
        ListNonFavoritedElements = 128
    };
    Q_DECLARE_FLAGS(ListObjectsOptions, ListObjectsOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOptions options);

    /**
     * @brief switchUser - switches to another local storage database file
     * associated with the passed in account
     *
     * If optional "startFromScratch" parameter is set to true (it is false
     * by default), the database file would be erased and only then - opened.
     * If optional "overrideLock" parameter is set to true, the advisory lock
     * set on the database file (if any) would be forcefully removed; otherwise,
     * if this parameter if set to false, the presence of advisory lock on
     * the database file woud cause the method to throw DatabaseLockedException
     *
     * @param account           The account to which the local storage is to be
     *                          switched
     * @param options           Startup options for the local storage, none
     *                          enabled by default
     */
    void switchUser(
        const Account & account,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        const StartupOptions options = {});
#else
        const StartupOptions options = 0);
#endif

    /**
     * isLocalStorageVersionTooHigh method checks whether the existing local
     * storage persistence has version which is too high for the currenly run
     * version of libquentier to work with i.e. whether the local storage has
     * already been upgraded using a new version of libquentier.
     *
     * NOTE: it is libquentier client code's responsibility to call this method
     * and/or localStorageRequiresUpgrade method, libquentier won't call any of
     * these on its own and will just attempt to work with the existing local
     * storage, whatever version it is of. If version is too high, things can
     * fail in most mysterious way, so the client code is obliged to call these
     * methods to ensure the local storage version is checked properly.
     *
     * @param errorDescription      Textual description of the error if
     *                              the method was unable to determine whether
     *                              the local storage version is too high for
     *                              the currently run version of libquentier
     *                              to work with, otherwise this parameter
     *                              is not touched by the method
     * @return                      True if local storage version is too high
     *                              for the currently run version of libquentier
     *                              to work with, false otherwise
     */
    bool isLocalStorageVersionTooHigh(ErrorString & errorDescription);

    /**
     * localStorageRequiresUpgrade method checks whether the existing local
     * storage persistence requires to be upgraded. The upgrades may be required
     * sometimes when new version of libquentier is rolled out which changes
     * something in the internals of local storage organization. This method
     * only checks for changes which are backwards incompatible i.e. once
     * the local storage is upgraded, previous version of libquentier won't be
     * able to work with it properly!
     *
     * NOTE: it is libquentier client code's responsibility to call this method
     * and/or isLocalStorageVersionTooHigh method, libquentier won't call any
     * of these on its own and will just attempt to work with the existing local
     * storage, whatever version it is of. If version is too high, things can
     * fail in most mysterious way, so the client code is obliged to call these
     * methods to ensure the local storage version is checked properly.
     *
     * @param errorDescription      Textual description of the error if
     *                              the method was unable to determine whether
     *                              the local storage requires upgrade,
     *                              otherwise this parameter is not touched by
     *                              the method
     * @return                      True if local storage requires upgrade,
     *                              false otherwise
     */
    bool localStorageRequiresUpgrade(ErrorString & errorDescription);

    /**
     * requiredLocalStoragePatches provides the client code with the list of
     * patches which need to be applied to the current state of local storage
     * in order to bring it to a state compatible with the current version of
     * code. If no patches are required, an empty list of patches is returned.
     *
     * The client code should apply each patch in the exact order in which they
     * are returned by this method.
     *
     * @return                      The vector of patches required to be applied
     *                              to the current local storage version
     */
    QVector<std::shared_ptr<ILocalStoragePatch>> requiredLocalStoragePatches();

    /**
     * localStorageVersion method fetches the current version of local storage
     * persistence which can be used for informational purposes.
     *
     * @param errorDescription      Textual description of the error if
     *                              the method was unable to determine
     *                              the current version of local storage
     *                              persistence
     * @return                      Positive number indication local storage
     *                              version or negative number in case of error
     *                              retrieving the local storage version
     */
    qint32 localStorageVersion(ErrorString & errorDescription);

    /**
     * highestSupportedLocalStorageVersion returns the highest version of local
     * storage persistence which the current build of libquentier is capable of
     * working with
     *
     * @return                      Highest supported local storage version
     */
    qint32 highestSupportedLocalStorageVersion() const;

    /**
     * @brief userCount returns the number of non-deleted users currently stored
     * in the local storage database
     *
     * @param errorDescription      Error description if the number of users
     *                              could not be returned
     * @return                      Either non-negative value with the number of
     *                              users or -1 which means some error has
     *                              occurred
     */
    int userCount(ErrorString & errorDescription) const;

    /**
     * @brief addUser adds the passed in User object to the local storage
     * database
     *
     * The table with Users is only involved in operations with notebooks which
     * have "contact" field set which in turn is used with business accounts
     *
     * @param user                  The user to be added to the local storage
     *                              database
     * @param errorDescription      Error description if the user could not be
     *                              added
     * @return                      True if the user was added successfully,
     *                              false otherwise
     */
    bool addUser(const User & user, ErrorString & errorDescription);

    /**
     * @brief updateUser updates the passed in User object in the local storage
     * database
     *
     * The table with Users is only involved in operations with notebooks which
     * have "contact" field set which in turn is used with business accounts
     *
     * @param user                  The user to be updated in the local storage
     *                              database
     * @param errorDescription      Error description if the user could not be
     *                              updated
     * @return                      True if the user was updated successfully,
     *                              false otherwise
     */
    bool updateUser(const User & user, ErrorString & errorDescription);

    /**
     * @brief findUser attempts to find and fill the fields of the passed in
     * User object which must have "id" field set as this value is used as
     * the identifier of User objects in the local storage database
     *
     * @param user                  The user to be found. Must have "id" field
     *                              set
     * @param errorDescription      Error description if the user could not be
     *                              found
     * @return                      True if the user was found successfully,
     *                              false otherwise
     */
    bool findUser(User & user, ErrorString & errorDescription) const;

    /**
     * @brief deleteUser marks the user as deleted in local storage
     *
     * @param user                  The user to be marked as deleted
     * @param errorDescription      Error description if the user could not be
     *                              marked as deleted
     * @return                      True if the user was marked as deleted
     *                              successfully, false otherwise
     */
    bool deleteUser(const User & user, ErrorString & errorDescription);

    /**
     * @brief expungeUser permanently deletes the user from the local storage
     * database
     *
     * @param user                  The user to be expunged
     * @param errorDescription      Error description if the user could not
     *                              be expunged
     * @return                      True if the user was expunged successfully,
     *                              false otherwise
     */
    bool expungeUser(const User & user, ErrorString & errorDescription);

    /**
     * @brief notebookCount returns the number of notebooks currently stored
     * in the local storage database
     *
     * @param errorDescription      Error description if the number of notebooks
     *                              could not be returned
     * @return                      Either non-negative value with the number of
     *                              notebooks or -1 which means some error
     *                              has occurred
     */
    int notebookCount(ErrorString & errorDescription) const;

    /**
     * @brief addNotebook adds the passed in Notebook to the local storage
     * database
     *
     * If the notebook has "remote" Evernote service's guid set, it is
     * identified by this guid in the local storage database. Otherwise it is
     * identified by the local uid
     *
     * @param notebook              The notebook to be added to the local
     *                              storage database; the object is passed by
     *                              reference and may be changed as a result of
     *                              the call (filled with autocompleted fields
     *                              like local uid if it was empty before
     *                              the call)
     * @param errorDescription      Error description if the notebook could not
     *                              be added
     * @return                      True if the notebook was added successfully,
     *                              false otherwise
     */
    bool addNotebook(Notebook & notebook, ErrorString & errorDescription);

    /**
     * @brief updateNotebook updates the passed in Notebook in the local storage
     * database
     *
     * If the notebook has "remote" Evernote service's guid set, it is
     * identified by this guid in the local storage database. Otherwise it is
     * identified by the local uid.
     *
     * @param notebook              Notebook to be updated in the local storage
     *                              database; the object is passed by reference
     *                              and may be changed as a result of the call
     *                              (filled with autocompleted fields like local
     *                              uid if it was empty before the call)
     * @param errorDescription      Error description if the notebook could not
     *                              be updated
     * @return                      True if the notebook was updated
     *                              successfully, false otherwise
     */
    bool updateNotebook(Notebook & notebook, ErrorString & errorDescription);

    /**
     * @brief findNotebook attempts to find and set all found fields of
     * the passed in Notebook object
     *
     * If "remote" Evernote service's guid for the notebook is set,
     * it is used to identify the notebook in the local storage database.
     * Otherwise the notebook is identified by its local uid. If it's empty,
     * the search would attempt to find the notebook by its name. If the name
     * is also not set, the search would attempt to find the notebook by
     * linked notebook guid assuming that no more than one notebook corresponds
     * to the linked notebook guid. If linked notebook guid is also
     * not set, the search would fail.
     *
     * Important! Due to the fact that the notebook name is only unique within
     * the users's own account as well as within each linked notebook, the
     * result of the search by name depends on the notebook's linked notebook
     * guid: if it is not set, the search by name would only search for
     * the notebook with the specified name within the user's own account.
     * If it is set, the search would only consider the linked notebook with
     * the corresponding guid.
     *
     * @param notebook              The notebook to be found. Must have either
     *                              "remote" or local uid or name or linked
     *                              notebook guid set
     * @param errorDescription      Error description if the notebook could not
     *                              be found
     * @return                      True if the notebook was found, false
     *                              otherwise
     */
    bool findNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    /**
     * @brief findDefaultNotebook attempts to find the default notebook
     * in the local storage database.
     *
     * @param notebook              The default notebook to be found
     * @param errorDescription      Error description if the default notebook
     *                              could not be found
     * @return                      True if the default notebook was found,
     *                              false otherwise
     */
    bool findDefaultNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    /**
     * @brief findLastUsedNotebook attempts to find the last used notebook
     * in the local storage database.
     *
     * @param notebook              The last used notebook to be found
     * @param errorDescription      Error description if the last used notebook
     *                              could not be found
     * @return                      True if the last used notebook was found,
     *                              false otherwise
     */
    bool findLastUsedNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    /**
     * @brief findDefaultOrLastUsedNotebook attempts to find either the default
     * or the last used notebook in the local storage database.
     *
     * @param notebook              Either the default or the last used notebook
     *                              to be found
     * @param errorDescription      Error description if the default or the last
     *                              used notebook could not be found
     * @return                      True if the default or the last used
     *                              notebook were found, false otherwise
     */
    bool findDefaultOrLastUsedNotebook(
        Notebook & notebook, ErrorString & errorDescription) const;

    /**
     * @brief The OrderDirection enum specifies the direction of ordering of
     * the results for methods listing the objects from the local storage
     * database
     */
    enum class OrderDirection
    {
        Ascending = 0,
        Descending
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const OrderDirection orderDirection);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const OrderDirection orderDirection);

    /**
     * @brief The ListNotebooksOrder allows to specify the results ordering for
     * methods listing notebooks from the local storage database
     */
    enum class ListNotebooksOrder
    {
        ByUpdateSequenceNumber = 0,
        ByNotebookName,
        ByCreationTimestamp,
        ByModificationTimestamp,
        NoOrder
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListNotebooksOrder order);

    /**
     * @brief listAllNotebooks attempts to list all notebooks within the current
     * account from the local storage database.
     *
     * @param errorDescription      Error description if all notebooks
     *                              could not be listed; if no error happens,
     *                              this parameter is untouched
     * @param limit                 The limit for the max number of notebooks
     *                              in the result, zero by default which
     *                              means no limit is set
     * @param offset                The number of notebooks to skip
     *                              in the beginning of the result, zero
     *                              by default
     * @param order                 Allows to specify a particular ordering
     *                              of notebooks in the result, NoOrder by
     *                              default
     * @param orderDirection        Specifies the direction of ordering,
     *                              by default ascending direction is used;
     *                              this parameter has no meaning if order
     *                              is equal to NoOrder
     * @param linkedNotebookGuid    If it's null, the method would list
     *                              the notebooks ignoring their belonging
     *                              to the current account or to some
     *                              linked notebook; if it's empty, only
     *                              the non-linked notebooks  would be listed;
     *                              otherwise, the only one notebook from
     *                              the corresponding linked notebook
     *                              would be listed
     * @return                      Either the list of all notebooks within
     *                              the account or empty list in cases of
     *                              error or no notebooks presence within
     *                              the account
     */
    QList<Notebook> listAllNotebooks(
        ErrorString & errorDescription, const size_t limit = 0,
        const size_t offset = 0,
        const ListNotebooksOrder order = ListNotebooksOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief listNotebooks attempts to list notebooks within the account
     * according to the specified input flag
     *
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notebooks to be listed
     * @param errorDescription      Error description if notebooks within
     *                              the account could not be listed; if no error
     *                              happens, this parameter is untouched
     * @param limit                 The limit for the max number of notebooks
     *                              in the result, zero by default which means
     *                              no limit is set
     * @param offset                The number of notebooks to skip in the
     *                              beginning of the result, zero by default
     * @param order                 Allows to specify a particular ordering of
     *                              notebooks in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @param linkedNotebookGuid    If it's null, the method would list
     *                              notebooks ignoring their belonging to
     *                              the current account or to some linked
     *                              notebook; if it's empty, only the non-linked
     *                              notebooks would be listed; otherwise,
     *                              the only one notebook from the corresponding
     *                              linked notebook would be listed
     * @return                      Either the list of notebooks within
     *                              the account conforming to the filter or
     *                              empty list in cases of error or no notebooks
     *                              conforming to the filter exist within
     *                              the account
     */
    QList<Notebook> listNotebooks(
        const ListObjectsOptions flag, ErrorString & errorDescription,
        const size_t limit = 0, const size_t offset = 0,
        const ListNotebooksOrder order = ListNotebooksOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief listAllSharedNotebooks attempts to list all shared notebooks
     * within the account.
     *
     * @param errorDescription      Error description if shared notebooks could
     *                              not be listed; if no error happens, this
     *                              parameter is untouched
     * @return                      Either the list of all shared notebooks
     *                              within the account or empty list in cases of
     *                              error or no shared notebooks presence
     *                              within the account
     */
    QList<SharedNotebook> listAllSharedNotebooks(
        ErrorString & errorDescription) const;

    /**
     * @brief listSharedNotebooksPerNotebookGuid - attempts to list all shared
     * notebooks per given notebook's remote guid (not local uid, it's
     * important).
     *
     * @param notebookGuid          Remote Evernote service's guid of
     *                              the notebook for which the shared notebooks
     *                              are requested
     * @param errorDescription      Error description if shared notebooks per
     *                              notebook guid could not be listed; if no
     *                              error happens, this parameter is untouched
     * @return                      Either the list of shared notebooks per
     *                              notebook guid or empty list in case of error
     *                              or no shared notebooks presence per given
     *                              notebook guid
     */
    QList<SharedNotebook> listSharedNotebooksPerNotebookGuid(
        const QString & notebookGuid, ErrorString & errorDescription) const;

    /**
     * @brief expungeNotebook permanently deletes the specified notebook from
     * the local storage database.
     *
     * Evernote API doesn't allow to delete the notebooks from the remote
     * storage, it can only be done by the official desktop Evernote client or
     * via its web client. So this method should be called only during
     * the synchronization with the remote storage, when some notebook is found
     * to be deleted via either the official desktop client or via the web
     * client; also, this method can be called for local notebooks not
     * synchronized with Evernote at all.
     *
     * @param notebook              The notebook to be expunged. Must have
     *                              either "remote" guid or local uid set;
     *                              the object is passed by reference and may
     *                              be changed as a result of the call (filled
     *                              with local uid if it was empty before
     *                              the call)
     * @param errorDescription      Error description if the notebook could
     *                              not be expunged
     * @return                      True if the notebook was expunged
     *                              successfully, false otherwise
     */
    bool expungeNotebook(Notebook & notebook, ErrorString & errorDescription);

    /**
     * @brief linkedNotebookCount returns the number of linked notebooks stored
     * in the local storage database.
     *
     * @param errorDescription      Error description if the number of linked
     *                              notebooks count not be returned
     * @return                      Either non-negative number of linked
     *                              notebooks or -1 if some error has occurred
     */
    int linkedNotebookCount(ErrorString & errorDescription) const;

    /**
     * @brief addLinkedNotebook adds passed in LinkedNotebook to the local
     * storage database; LinkedNotebook must have "remote" Evernote service's
     * guid set. It is not possible to add a linked notebook in offline mode so
     * it doesn't make sense for LinkedNotebook objects to not have guid.
     *
     * @param linkedNotebook        LinkedNotebook to be added to the local
     *                              storage database
     * @param errorDescription      Error description if linked notebook could
     *                              not be added
     * @return                      True if linked notebook was added
     *                              successfully, false otherwise
     */
    bool addLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    /**
     * @brief updateLinkedNotebook updates passd in LinkedNotebook in the local
     * storage database; LinkedNotebook must have "remote" Evernote service's
     * guid set.
     *
     * @param linkedNotebook        LinkedNotebook to be updated in the local
     *                              storage database
     * @param errorDescription      Error description if linked notebook could
     *                              not be updated
     * @return                      True if linked notebook was updated
     *                              successfully, false otherwise
     */
    bool updateLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    /**
     * @brief findLinkedNotebook attempts to find and set all found fields for
     * passed in by reference LinkedNotebook object. For LinkedNotebook local
     * uid doesn't mean anything because it can only be considered valid if it
     * has "remote" Evernote service's guid set. So this passed in
     * LinkedNotebook object must have guid set to identify the linked notebook
     * in the local storage database.
     *
     * @param linkedNotebook        Linked notebook to be found. Must have
     *                              "remote" guid set
     * @param errorDescription      Error description if linked notebook could
     *                              not be found
     * @return                      True if linked notebook was found,
     *                              false otherwise
     */
    bool findLinkedNotebook(
        LinkedNotebook & linkedNotebook, ErrorString & errorDescription) const;

    /**
     * @brief The ListLinkedNotebooksOrder enum allows to specify the results
     * ordering for methods listing linked notebooks from local storage
     */
    enum class ListLinkedNotebooksOrder
    {
        ByUpdateSequenceNumber = 0,
        ByShareName,
        ByUsername,
        NoOrder
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListLinkedNotebooksOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListLinkedNotebooksOrder order);

    /**
     * @brief listAllLinkedNotebooks - attempts to list all linked notebooks
     * within the account.
     *
     * @param errorDescription      Error description if linked notebooks could
     *                              not be listed, otherwise this parameter
     *                              is untouched
     * @param limit                 Limit for the max number of linked notebooks
     *                              in the result, zero by default which means
     *                              no limit is set
     * @param offset                Number of linked notebooks to skip in
     *                              the beginning of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              linked notebooks in the result, NoOrder
     *                              by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @return                      Either list of all linked notebooks or empty
     *                              list in case of error or no linked notebooks
     *                              presence within the account
     */
    QList<LinkedNotebook> listAllLinkedNotebooks(
        ErrorString & errorDescription, const size_t limit = 0,
        const size_t offset = 0,
        const ListLinkedNotebooksOrder order =
            ListLinkedNotebooksOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending) const;

    /**
     * @brief listLinkedNotebooks attempts to list linked notebooks within
     * the account according to the specified input flag.
     *
     * @param flag                  Input parameter used to set the filter for
     *                              the desired linked notebooks to be listed
     * @param errorDescription      Error description if linked notebooks within
     *                              the account could not be listed; if no error
     *                              happens, this parameter is untouched
     * @param limit                 Limit for the max number of linked notebooks
     *                              in the result, zero by default which means
     *                              no limit is set
     * @param offset                Number of linked notebooks to skip in
     *                              the beginning of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              linked notebooks in the result, NoOrder
     *                              by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @return                      Either list of linked notebooks within
     *                              the account conforming to the filter or
     *                              empty list in cases of error or no linked
     *                              notebooks conforming to the filter exist
     *                              within the account
     */
    QList<LinkedNotebook> listLinkedNotebooks(
        const ListObjectsOptions flag, ErrorString & errorDescription,
        const size_t limit = 0, const size_t offset = 0,
        const ListLinkedNotebooksOrder order =
            ListLinkedNotebooksOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending) const;

    /**
     * @brief expungeLinkedNotebook permanently deletes specified linked
     * notebook from the local storage database.
     *
     * Evernote API doesn't allow to delete linked notebooks from the remote
     * storage, it can only be done by official desktop client or web client.
     * So this method should be called only during the synchronization with
     * remote service, when some linked notebook is found to be deleted via
     * either official desktop client or web cient.
     *
     * @param linkedNotebook        Linked notebook to be expunged. Must have
     *                              "remote" guid set
     * @param errorDescription      Error description if linked notebook could
     *                              not be expunged
     * @return                      True if linked notebook was expunged
     *                              successfully, false otherwise
     */
    bool expungeLinkedNotebook(
        const LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    /**
     * @brief The NoteCountOption enum is a QFlags enum which allows to specify
     * some options for methods returning note counts from local storage
     */
    enum class NoteCountOption
    {
        IncludeNonDeletedNotes = 1,
        IncludeDeletedNotes = 2
    };
    Q_DECLARE_FLAGS(NoteCountOptions, NoteCountOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const NoteCountOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const NoteCountOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const NoteCountOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const NoteCountOptions options);

    /**
     * @brief noteCount returns the number of notes currently
     * stored in the local storage database.
     *
     * @param errorDescription      Error description if the number of notes
     *                              could not be returned
     * @param options               Options clarifying which notes to list;
     *                              by default only non-deleted notes are listed
     * @return                      Either non-negative value with the number
     *                              of notes or -1 which means some error
     *                              occurred
     */
    int noteCount(
        ErrorString & errorDescription,
        const NoteCountOptions options =
            NoteCountOption::IncludeNonDeletedNotes) const;

    /**
     * @brief noteCountPerNotebook returns the number of notes currently
     * stored in the local storage database per given notebook.
     *
     * @param notebook              Notebook for which the number of notes is
     *                              requested. If its guid is set, it is used to
     *                              identify the notebook, otherwise its local
     *                              uid is used
     * @param errorDescription      Error description if the number of notes per
     *                              given notebook could not be returned
     * @param options               Options clarifying which notes to list;
     *                              by default only non-deleted notes are listed
     * @return                      Either non-negative value with the number of
     *                              notes per given notebook or -1 which means
     *                              some error occurred
     */
    int noteCountPerNotebook(
        const Notebook & notebook, ErrorString & errorDescription,
        const NoteCountOptions options =
            NoteCountOption::IncludeNonDeletedNotes) const;

    /**
     * @brief noteCountPerTag returns the number of notes currently
     * stored in local storage database labeled with given tag.
     *
     * @param tag                   Tag for which the number of notes labeled
     *                              with it is requested. If its guid is set,
     *                              it is used to identify the tag, otherwise
     *                              its local uid is used
     * @param errorDescription      Error description if the number of notes per
     *                              given tag could not be returned
     * @param options               Options clarifying which notes to list;
     *                              by default only non-deleted notes are listed
     * @return                      Either non-negative value with the number of
     *                              notes per given tag or -1 which means some
     *                              error occurred
     */
    int noteCountPerTag(
        const Tag & tag, ErrorString & errorDescription,
        const NoteCountOptions options =
            NoteCountOption::IncludeNonDeletedNotes) const;

    /**
     * @brief noteCountsPerAllTags returns the number of notes
     * currently stored in local storage database labeled with each tag stored
     * in the local storage database.
     *
     * @param noteCountsPerTagLocalUid      The result hash: note counts by tag
     *                                      local uids
     * @param errorDescription              Error description if the number of
     *                                      notes per all tags could not be
     *                                      returned
     * @param options                       Options clarifying which notes to
     *                                      list; by default only non-deleted
     *                                      notes are listed
     * @return                              True if note counts for all tags
     *                                      were computed successfully, false
     *                                      otherwise
     */
    bool noteCountsPerAllTags(
        QHash<QString, int> & noteCountsPerTagLocalUid,
        ErrorString & errorDescription,
        const NoteCountOptions options =
            NoteCountOption::IncludeNonDeletedNotes) const;

    /**
     * @brief noteCountPerNotebooksAndTags returns the number of notes currently
     * stored in local storage database belonging to one of notebooks
     * corresponding to given notebook local uids and labeled by at least one of
     * tags corresponding to given tag local uids
     *
     * @param notebookLocalUids     The list of notebook local uids used for
     *                              filtering
     * @param tagLocalUids          The list of tag local uids used for
     *                              filtering
     * @param errorDescription      Error description if the number of notes per
     *                              notebooks and tags could not be returned
     * @param options               Options clarifying which notes to list;
     *                              by default only non-deleted notes are listed
     * @return                      Either non-negative value with the number of
     *                              notes per given tag or -1 which means some
     *                              error occurred
     */
    int noteCountPerNotebooksAndTags(
        const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
        ErrorString & errorDescription,
        const NoteCountOptions options =
            NoteCountOption::IncludeNonDeletedNotes) const;

    /**
     * @brief addNote adds passed in Note to the local storage database.
     *
     * @param note                  Note to be added to local storage database;
     *                              required to contain either "remote" notebook
     *                              guid or local notebook uid; may be changed
     *                              as a result of the call, filled with
     *                              autogenerated fields like local uid if it
     *                              was empty before the call; also tag guids
     *                              are filled if the note passed in contained
     *                              only tag local uids and tag local uids are
     *                              filled if the note passed in contained only
     *                              tag guids
     * @param errorDescription      Error description if note could not be added
     * @return                      True if note was added successfully,
     *                              false otherwise
     */
    bool addNote(Note & note, ErrorString & errorDescription);

    /**
     * @brief The UpdateNoteOption enum is a QFlags enum which allows to specify
     * which note fields should be updated when updateNote method is called
     *
     * Most note data is updated unconditionally - note title, content,
     * attributes (if any) etc. However, some specific data can be chosen to
     * not update - notably, metadata of resources, binary data of resources or
     * lists of note's tags
     */
    enum class UpdateNoteOption
    {
        /**
         * UpdateResourceMetadata value specifies that fields aside dataBody,
         * dataSize, dataHash, alternateDataBody, alternateDataSize,
         * alternateDataHash for each note's resource should be updated
         */
        UpdateResourceMetadata = 1,
        /**
         * UpdateResourceBinaryData value specifies that dataBody, its size
         * and hash and alternateDataBody, its size and hash should be updated
         * for each of note's resources; this value only has effect if flags
         * also have UpdateResourceMetadata value enabled!
         */
        UpdateResourceBinaryData = 2,
        /**
         * UpdateTags value specifies that note's tag lists should be updated
         */
        UpdateTags = 4
    };
    Q_DECLARE_FLAGS(UpdateNoteOptions, UpdateNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const UpdateNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const UpdateNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const UpdateNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const UpdateNoteOptions options);

    /**
     * @brief updateNote updates passed in Note in the local storage database.
     *
     * If the note has "remote" Evernote service's guid set, it is identified
     * by this guid in the local storage database. If no note with such guid is
     * found, the local uid is used to identify the note in the local storage
     * database. If the note has no guid, the local uid is used to identify it
     * in the local storage database.
     *
     * A special way in which this method might be used is the update of a note
     * which clears note's guid. This way is special because it imposes certain
     * requirements onto the resources which the note might have.  However, it
     * is only relevant if options input parameter has UpdateResourceMetadata
     * flag enabled. The requirements for this special case are as follows:
     *   - each resource should not have noteGuid field set to a non-empty value
     *   - each resource should not have guid field set to a non-empty value as
     *     it makes no sense for note without guid i.e. note not synchronized
     *     with Evernote to own a resource which has guid i.e. is synchronized
     *     with Evernote
     *
     * @param note                  Note to be updated in the local storage
     *                              database; required to contain either
     * "remote" notebook guid or local notebook uid; may be changed as a result
     * of the call, filled with fields like local uid or notebook guid or local
     * uid if any of these were empty before the call; also tag guids are filled
     * if the note passed in contained only tag local uids and tag local uids
     * are filled if the note passed in contained only tag guids. Bear in mind
     * that after the call the note may not have the representative resources if
     *                              "updateNoteOptions" input parameter
     *                              contained no "UpdateResourceMetadata" flag
     *                              as well as it may not have
     *                              the representative tags if "UpdateTags" flag
     *                              was not set
     * @param options               Options specifying which optionally
     *                              updatable fields of the note should actually
     *                              be updated
     * @param errorDescription      Error description if note could not be
     *                              updated
     * @return                      True if note was updated successfully,
     *                              false otherwise
     */
    bool updateNote(
        Note & note, const UpdateNoteOptions options,
        ErrorString & errorDescription);

    /**
     * @brief The GetNoteOption enum is a QFlags enum which allows to specify
     * which note fields should be included when findNote or one of listNote*
     * methods is called
     *
     * Most note data is included unconditionally - note title, content,
     * attributes (if any) etc. However, some specific data can be opted to not
     * be included into the returned note data - notably, metadata of resources
     * and binary data of resources. If these are omitted, findNote or any of
     * listNote* methods might work faster than otherwise
     */
    enum class GetNoteOption
    {
        /**
         * WithResourceMetadata value specifies that fields aside dataBody,
         * dataSize, dataHash, alternateDataBody, alternateDataSize,
         * alternateDataHash for each note's resource should be included
         */
        WithResourceMetadata = 1,
        /**
         * WithResourceBinaryData value specifies that dataBody, its size
         * and hash and alternateDataBody, its size and hash should be included
         * into each of note's resources; this value only has effect if flags
         * also have WithResourceMetadata value enabled!
         */
        WithResourceBinaryData = 2
    };
    Q_DECLARE_FLAGS(GetNoteOptions, GetNoteOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const GetNoteOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const GetNoteOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const GetNoteOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const GetNoteOptions options);

    /**
     * @brief findNote - attempts to find note in the local storage database
     * @param note - note to be found in the local storage database. Must have
     * either local or "remote" Evernote service's guid set
     * @param options - options specifying which optionally includable fields
     * of the note should actually be included
     * @param errorDescription - error description if note could not be found
     * @return true if note was found successfully, false otherwise
     */
    bool findNote(
        Note & note, const GetNoteOptions options,
        ErrorString & errorDescription) const;

    /**
     * @brief The ListNotesOrder enum allows to specify the results ordering for
     * methods listing notes from the local storage database
     */
    enum class ListNotesOrder
    {
        ByUpdateSequenceNumber = 0,
        ByTitle,
        ByCreationTimestamp,
        ByModificationTimestamp,
        ByDeletionTimestamp,
        ByAuthor,
        BySource,
        BySourceApplication,
        ByReminderTime,
        ByPlaceName,
        NoOrder
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListNotesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListNotesOrder order);

    /**
     * @brief listNotesPerNotebook attempts to list notes per given notebook
     * @param notebook              Notebook for which the list of notes is
     *                              requested. If it has the "remote" Evernote
     *                              service's guid set, it would be used to
     *                              identify the notebook in the local storage
     *                              database, otherwise its local uid would be
     *                              used
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description in case notes could not
     *                              be listed
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notes to be listed
     * @param limit                 Limit for the max number of notes in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of notes to skip in the beginning
     *                              of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              notes in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used;
     * @return                      Either list of notes per notebook or empty
     *                              list in case of error or no notes presence
     *                              in the given notebook
     */
    QList<Note> listNotesPerNotebook(
        const Notebook & notebook, const GetNoteOptions options,
        ErrorString & errorDescription,
        const ListObjectsOptions & flag = ListObjectsOption::ListAll,
        const size_t limit = 0, const size_t offset = 0,
        const ListNotesOrder & order = ListNotesOrder::NoOrder,
        const OrderDirection & orderDirection =
            OrderDirection::Ascending) const;

    /**
     * @brief listNotesPerTag attempts to list notes labeled with a given tag
     * @param tag                   Tag for which the list of notes labeled with
     *                              it is requested. If it has the "remote"
     *                              Evernote service's guid set, it is used to
     *                              identify the tag in the local storage
     *                              database, otherwise its local uid is used
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description in case notes could not
     *                              be listed
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notes to be listed
     * @param limit                 Limit for the max number of notes in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of notes to skip in the beginning of
     *                              the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              notes in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used;
     * @return                      Either list of notes per tag or empty list
     *                              in case of error or no notes labeled with
     *                              the given tag presence
     */
    QList<Note> listNotesPerTag(
        const Tag & tag, const GetNoteOptions options,
        ErrorString & errorDescription,
        const ListObjectsOptions & flag = ListObjectsOption::ListAll,
        const size_t limit = 0, const size_t offset = 0,
        const ListNotesOrder & order = ListNotesOrder::NoOrder,
        const OrderDirection & orderDirection =
            OrderDirection::Ascending) const;

    /**
     * @brief listNotesPerNotebooksAndTags attempts to list notes which are
     * present within one of specified notebooks and are labeled with at least
     * one of specified tags
     *
     * @param notebookLocalUids     Local uids of notebooks to which the listed
     *                              notes might belong
     * @param tagLocalUids          Local uids of tags with which the listed
     *                              notes might be labeled
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description in case notes could not
     *                              be listed
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notes to be listed
     * @param limit                 Limit for the max number of notes in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of notes to skip in the beginning of
     *                              the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              notes in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used;
     * @return                      Either list of notes per notebooks and tags
     *                              or empty list in case of error or no notes
     *                              corresponding to given notebooks and tags
     *                              presence
     */
    QList<Note> listNotesPerNotebooksAndTags(
        const QStringList & notebookLocalUids, const QStringList & tagLocalUids,
        const GetNoteOptions options, ErrorString & errorDescription,
        const ListObjectsOptions & flag = ListObjectsOption::ListAll,
        const size_t limit = 0, const size_t offset = 0,
        const ListNotesOrder & order = ListNotesOrder::NoOrder,
        const OrderDirection & orderDirection =
            OrderDirection::Ascending) const;

    /**
     * @brief listNotesByLocalUids attempts to list notes given their local uids
     *
     * The method would only return notes which it managed to find within
     * the local storage i.e. having an invalid local uid in the list won't
     * result in an error, just in the corresponding note not returned
     * within the result
     *
     * Notes within the result can be additionally filtered with flag parameter
     *
     * @param noteLocalUids         Local uids of notes to be listed
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description in case notes could not
     *                              be listed
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notes to be listed
     * @param limit                 Limit for the max number of notes in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of notes to skip in the beginning of
     *                              the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              notes in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used;
     * @return                      Either list of notes by local uids or empty
     *                              list in case of error or no notes
     *                              corresponding to given local uids presence
     */
    QList<Note> listNotesByLocalUids(
        const QStringList & noteLocalUids, const GetNoteOptions options,
        ErrorString & errorDescription,
        const ListObjectsOptions & flag = ListObjectsOption::ListAll,
        const size_t limit = 0, const size_t offset = 0,
        const ListNotesOrder & order = ListNotesOrder::NoOrder,
        const OrderDirection & orderDirection =
            OrderDirection::Ascending) const;

    /**
     * @brief listNotes attempts to list notes within the account according to
     * the specified input flag.
     *
     * @param flag                  Input parameter used to set the filter for
     *                              the desired notes to be listed
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description if notes within
     *                              the account could not be listed; if no error
     *                              happens, this parameter is untouched
     * @param limit                 Limit for the max number of notes in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of notes to skip in the beginning
     *                              of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              notes in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @param linkedNotebookGuid    If it's null, notes from both user's own
     *                              notebooks and linked notebooks would be
     *                              listed; if it's empty, only the notes from
     *                              non-linked notebooks would be listed;
     *                              otherwise, only the notes from the specified
     *                              linked notebook would be listed
     * @return                      Either list of notes within the account
     *                              conforming to the filter or empty list in
     *                              cases of error or no notes conforming to
     *                              the filter exist within the account
     */
    QList<Note> listNotes(
        const ListObjectsOptions flag, const GetNoteOptions options,
        ErrorString & errorDescription, const size_t limit = 0,
        const size_t offset = 0,
        const ListNotesOrder order = ListNotesOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief findNoteLocalUidsWithSearchQuery attempts to find note local uids
     * of notes corresponding to the passed in NoteSearchQuery object.
     *
     * @param noteSearchQuery       Filled NoteSearchQuery object used to filter
     *                              the notes
     * @param errorDescription      Error description in case note local uids
     *                              could not be listed
     * @return                      The list of found notes' local uids or empty
     *                              list in case of error
     */
    QStringList findNoteLocalUidsWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery,
        ErrorString & errorDescription) const;

    /**
     * @brief findNotesWithSearchQuery attempts to find notes corresponding to
     * the passed in NoteSearchQuery object.
     *
     * @param noteSearchQuery       Filled NoteSearchQuery object used to filter
     *                              the notes
     * @param options               Options specifying which optionally
     *                              includable fields of the note should
     *                              actually be included
     * @param errorDescription      Error description in case notes could not
     *                              be listed
     * @return                      Either list of notes per NoteSearchQuery or
     *                              empty list in case of error or no notes
     *                              presence for the given NoteSearchQuery
     */
    NoteList findNotesWithSearchQuery(
        const NoteSearchQuery & noteSearchQuery, const GetNoteOptions options,
        ErrorString & errorDescription) const;

    /**
     * @brief expungeNote permanently deletes note from local storage.
     *
     * Evernote API doesn't allow to delete notes from the remote storage, it
     * can only be done by official desktop client or web client. So this method
     * should be called only during the synchronization with remote database,
     * when some note is found to be deleted via either official desktop client
     * or web client.
     *
     * @param note                  Note to be expunged; may be changed as a
     *                              result of the call, filled with fields like
     *                              local uid or notebook guid or local uid
     * @param errorDescription      Error description if note could not be
     *                              expunged
     * @return                      True if note was expunged successfully,
     *                              false otherwise
     */
    bool expungeNote(Note & note, ErrorString & errorDescription);

    /**
     * @brief tagCount returns the number of non-deleted tags currently stored
     * in the local storage database.
     *
     * @param errorDescription      Error description if the number of tags
     *                              could not be returned
     * @return                      Either non-negative value with the number of
     *                              tags or -1 which means some error occurred
     */
    int tagCount(ErrorString & errorDescription) const;

    /**
     * @brief addTag adds passed in Tag to the local storage database. If tag
     * has "remote" Evernote service's guid set, it is identified in
     * the database by this guid. Otherwise it is identified by local uid.
     *
     * @param tag                   Tag to be added to the local storage; may be
     *                              changed as a result of the call, filled with
     *                              autogenerated fields like local uid if it
     *                              was empty before the call
     * @param errorDescription      Error description if Tag could not be added
     * @return                      True if Tag was added successfully,
     *                              false otherwise
     */
    bool addTag(Tag & tag, ErrorString & errorDescription);

    /**
     * @brief updateTag updates passed in Tag in the local storage database.
     *
     * If the tag has "remote" Evernote service's guid set, it is identified
     * by this guid in the local storage database. If the tag has no guid,
     * the local uid is used to identify it in the local storage database.
     *
     * @param tag                   Tag filled with values to be updated in
     *                              the local storage database. Note that it
     *                              can be changed * as a result of the call:
     *                              automatically filled with local uid if it
     *                              was empty before the call
     * @param errorDescription      Error description if tag could not be
     *                              updated
     * @return                      True if tag was updated successfully,
     *                              false otherwise
     */
    bool updateTag(Tag & tag, ErrorString & errorDescription);

    /**
     * @brief findTag attempts to find and fill the fields of passed in tag
     * object.
     *
     * If "remote" Evernote service's guid for the tag is set, it would be used
     * to identify the tag in the local storage database. Otherwise the local
     * uid would be used. If neither guid nor local uid are set, tag's name
     * would be used. If the name is also not set, the search would fail.
     *
     * Important! Due to the fact that the tag name is only unique within
     * the users's own account as well as within each linked notebook, the
     * result of the search by name depends on the tag's linked notebook
     * guid: if it is not set, the search by name would only search for the tag
     * with the specified name within the user's own account. If it is set,
     * the search would only consider tags from a linked notebook with
     * the corresponding guid.
     *
     * @param tag                   Tag to be found in the local storage
     *                              database; must have either guid, local uid
     *                              or name set
     * @param errorDescription      Error description in case tag could not be
     *                              found
     * @return                      True if tag was found, false otherwise
     */
    bool findTag(Tag & tag, ErrorString & errorDescription) const;

    /**
     * @brief The ListTagsOrder enum allows to specify the results ordering for
     * methods listing tags from the local storage database
     */
    enum class ListTagsOrder
    {
        ByUpdateSequenceNumber,
        ByName,
        NoOrder
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListTagsOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListTagsOrder order);

    /**
     * @brief listAllTagsPerNote lists all tags per given note
     * @param note                  Note for which the list of tags is
     *                              requested. If it has "remote" Evernote
     *                              service's guid set, it is used to identify
     *                              the note in the local storage database.
     *                              Otherwise its local uid is used for that.
     * @param errorDescription      Error description if tags were not listed
     *                              successfully.  In such case the returned
     *                              list of tags would be empty and error
     *                              description won't be empty. However, if,
     *                              for example, the list of tags is empty and
     *                              error description is empty too, it means
     *                              the provided note does not have any tags
     *                              assigned to it.
     * @param flag                  Input parameter used to set the filter for
     *                              the desired tags to be listed
     * @param limit                 Limit for the max number of tags in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of tags to skip in the beginning of
     *                              the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              tags in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used;
     * @return                      The list of found tags per note
     */
    QList<Tag> listAllTagsPerNote(
        const Note & note, ErrorString & errorDescription,
        const ListObjectsOptions & flag = ListObjectsOption::ListAll,
        const size_t limit = 0, const size_t offset = 0,
        const ListTagsOrder & order = ListTagsOrder::NoOrder,
        const OrderDirection & orderDirection =
            OrderDirection::Ascending) const;

    /**
     * @brief listAllTags lists all tags within the current user's account.
     * @param errorDescription      Error description if tags were not listed
     *                              successfully. In such case the returned list
     *                              of tags would be empty and error description
     *                              won't be empty. However, if, for example,
     *                              the list of tags is empty and error
     *                              description is empty too, it means
     *                              the current account does not have any tags
     *                              created.
     * @param limit                 Limit for the max number of tags in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of tags to skip in the beginning
     *                              of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              tags in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @param linkedNotebookGuid    If it's null, the method would list tags
     *                              ignoring their belonging to the current
     *                              account or to some linked notebook; if it's
     *                              empty, only the tags from user's own account
     *                              would be listed; otherwise, only the tags
     *                              corresponding to the certain linked notebook
     *                              would be listed
     * @return                      The list of found tags within the account
     */
    QList<Tag> listAllTags(
        ErrorString & errorDescription, const size_t limit = 0,
        const size_t offset = 0,
        const ListTagsOrder order = ListTagsOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief listTags attempts to list tags within the account according to
     * the specified input flag.
     *
     * @param flag                  Input parameter used to set the filter for
     *                              the desired tags to be listed
     * @param errorDescription      Error description if notes within
     *                              the account could not be listed; if no error
     *                              happens, this parameter is untouched
     * @param limit                 Limit for the max number of tags in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of tags to skip in the beginning
     *                              of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              tags in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @param linkedNotebookGuid    If it's null, the method would list tags
     *                              ignoring their belonging to the current
     *                              account or to some linked notebook; if it's
     *                              empty, only the tags from user's own account
     *                              would be listed; otherwise, only the tags
     *                              corresponding to the certain linked notebook
     *                              would be listed
     * @return                      Either list of tags within the account
     *                              conforming to the filter or empty list in
     *                              cases of error or no tags conforming to
     *                              the filter exist within the account
     */
    QList<Tag> listTags(
        const ListObjectsOptions flag, ErrorString & errorDescription,
        const size_t limit = 0, const size_t offset = 0,
        const ListTagsOrder & order = ListTagsOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief listTagsWithNoteLocalUids attempts to list tags and their
     * corresponding local uids within the account according to the specified
     * input flag
     *
     * The method is very similar to listTags only for each listed tag it
     * returns the list of note local uids corresponding to notes labeled with
     * the respective tag.
     *
     * @param flag                  Input parameter used to set the filter for
     *                              the desired tags to be listed
     * @param errorDescription      Error description if notes within
     *                              the account could not be listed; if no error
     *                              happens, this parameter is untouched
     * @param limit                 Limit for the max number of tags in
     *                              the result, zero by default which means no
     *                              limit is set
     * @param offset                Number of tags to skip in the beginning
     *                              of the result, zero by default
     * @param order                 Allows to specify particular ordering of
     *                              tags in the result, NoOrder by default
     * @param orderDirection        Specifies the direction of ordering, by
     *                              default ascending direction is used; this
     *                              parameter has no meaning if order is equal
     *                              to NoOrder
     * @param linkedNotebookGuid    If it's null, the method would list tags
     *                              ignoring their belonging to the current
     *                              account or to some linked notebook; if it's
     *                              empty, only the tags from user's own account
     *                              would be listed; otherwise, only the tags
     *                              corresponding to the certain linked notebook
     *                              would be listed
     * @return                      Either list of tags and note local uids
     *                              within the account conforming to the filter
     *                              or empty list in cases of error or no tags
     *                              conforming to the filter exist within
     *                              the account
     */
    QList<std::pair<Tag, QStringList>> listTagsWithNoteLocalUids(
        const ListObjectsOptions flag, ErrorString & errorDescription,
        const size_t limit = 0, const size_t offset = 0,
        const ListTagsOrder & order = ListTagsOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending,
        const QString & linkedNotebookGuid = QString()) const;

    /**
     * @brief expungeTag permanently deletes tag from the local storage
     * database.
     *
     * Evernote API doesn't allow to delete tags from remote storage, it can
     * only be done by official desktop client or web client. So this method
     * should be called only during the synchronization with remote database,
     * when some tag is found to be deleted via either official desktop client
     * or web client.
     *
     * @param tag                           Tag to be expunged; may be changed
     *                                      as a result of the call,
     *                                      automatically filled with local uid
     *                                      if it was empty before the call
     * @param expungedChildTagLocalUids     If the expunged tag was a parent of
     *                                      some other tags, these were expunged
     *                                      as well; this parameter would
     *                                      contain the local uids of expunged
     *                                      child tags
     * @param errorDescription              Error description if tag could not
     *                                      be expunged
     * @return                              True if tag was expunged
     *                                      successfully, false otherwise
     */
    bool expungeTag(
        Tag & tag, QStringList & expungedChildTagLocalUids,
        ErrorString & errorDescription);

    /**
     * @brief expungeNotelessTagsFromLinkedNotebooks permanently deletes from
     * the local storage database those tags which belong to some linked
     * notebook and are not linked with any notes.
     *
     * @param errorDescription          Error description if tag could not
     *                                  be expunged
     * @return                          True if relevant tags were expunged
     *                                  successfully, false otherwise
     */
    bool expungeNotelessTagsFromLinkedNotebooks(ErrorString & errorDescription);

    /**
     * @brief enResourceCount (the name is not Resource to prevent problems with
     * macro defined on some versions of Windows) returns the number of
     * resources currently stored in the local storage database.
     *
     * @param errorDescription          Error description if the number of
     *                                  resources could not be returned
     * @return                          Either non-negative value with
     *                                  the number of resources or -1 which
     *                                  means some error occurred
     */
    int enResourceCount(ErrorString & errorDescription) const;

    /**
     * @brief addEnResource adds passed in resource to the local storage
     * database.
     *
     * @param resource                  Resource to be added to the database,
     *                                  must have either note's local uid set
     *                                  or note's "remote" Evernote service's
     *                                  guid set; may be changed as a result of
     *                                  the call, filled with autogenerated
     *                                  fields like local uid if it was empty
     *                                  before the call
     * @param errorDescription          Error description if resource could
     *                                  not be added
     * @return                          True if resource was added successfully,
     *                                  false otherwise
     */
    bool addEnResource(Resource & resource, ErrorString & errorDescription);

    /**
     * @brief updateEnResource updates passed in resource in the local storage
     * database.
     *
     * If the resource has "remote" Evernote service's guid set, it is
     * identified by this guid in the local storage database. If no resource
     * with such guid is found, the local uid is used to identify the resource
     * in the local storage database. If the resource has no guid, the local
     * uid is used to identify it in the local storage database.
     *
     * @param resource                  Resource to be updated; may be changed
     *                                  as a result of the call, automatically
     *                                  filled with local uid and note local uid
     *                                  and/or guid if these were empty before
     *                                  the call
     * @param errorDescription          Error description if resource could not
     *                                  be updated
     * @return                          True if resource was updated
     *                                  successfully, false otherwise
     */
    bool updateEnResource(Resource & resource, ErrorString & errorDescription);

    /**
     * @brief The GetResourceOption enum is a QFlags enum which allows to
     * specify which resource fields should be included when findEnResource
     * method is called.
     *
     * Most resource data is included unconditionally but some specific data can
     * be opted to not be included into the returned resource data - notably,
     * binary data of the resource. If it is omitted, findEnResource method
     * might work faster than otherwise
     */
    enum class GetResourceOption
    {
        /**
         * WithBinaryData value specifies than dataBody and alternateDataBody
         * should be included into the returned resource
         */
        WithBinaryData = 1
    };
    Q_DECLARE_FLAGS(GetResourceOptions, GetResourceOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const GetResourceOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const GetResourceOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const GetResourceOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const GetResourceOptions options);

    /**
     * @brief findEnResource method attempts to find resource in the local
     * storage database
     *
     * @param resource              Resource to be found in the local storage
     *                              database. If it has the "remote" Evernote
     *                              service's guid set, this guid is used to
     *                              identify the resource in the local storage
     *                              database. Otherwise resource's local uid is
     *                              used
     * @param options               Options specifying which optionally
     *                              includable fields of the resource should
     *                              actually be included
     * @param errorDescription      Error description if resource could not be
     *                              found
     * @return                      True if resource was found successfully,
     *                              false otherwise
     */
    bool findEnResource(
        Resource & resource, const GetResourceOptions options,
        ErrorString & errorDescription) const;

    /**
     * @brief expungeResource permanently deletes resource from the local
     * storage database.
     *
     * @param resource                  Resource to be expunged; may be changed
     *                                  as a result of the call, automatically
     *                                  filled with local uid and note local
     *                                  uid and/or guid if these were empty
     *                                  before the call
     * @param errorDescription          Error description if resource could not
     *                                  be expunged
     * @return                          True if resource was expunged
     *                                  successfully, false otherwise
     */
    bool expungeEnResource(Resource & resource, ErrorString & errorDescription);

    /**
     * @brief savedSearchCount returns the number of saved seacrhes currently
     * stored in local storage database.
     * @param errorDescription          Error description if the number of saved
     *                                  seacrhes could not be returned
     * @return                          Either non-negative value with the
     *                                  number of saved seacrhes or -1 which
     *                                  means some error occurred
     */
    int savedSearchCount(ErrorString & errorDescription) const;

    /**
     * @brief addSavedSearch adds passed in SavedSearch to the local storage
     * database; if search has "remote" Evernote service's guid set, it is
     * identified in the database by this guid. Otherwise it is identified by
     * local uid.
     *
     * @param search                    SavedSearch to be added to the local
     *                                  storage; may be changed as a result of
     *                                  the call, filled with autogenerated
     *                                  fields like local uid if it was empty
     *                                  before the call
     * @param errorDescription          Error description if SavedSearch could
     *                                  not be added
     * @return                          True if SavedSearch was added
     *                                  successfully, false otherwise
     */
    bool addSavedSearch(SavedSearch & search, ErrorString & errorDescription);

    /**
     * @brief updateSavedSearch updates passed in SavedSearch in the local
     * storage database.
     *
     * If search has "remote" Evernote service's guid set, it is identified
     * in the database by this guid. If the saved search has no guid,
     * the local uid is used to identify it in the local storage database.
     *
     * @param search                    SavedSearch filled with values to be
     *                                  updated in the local storage database;
     *                                  may be changed as a result of the call
     *                                  filled local uid if it was empty before
     *                                  the call
     * @param errorDescription          Error description if SavedSearch could
     *                                  not be updated
     * @return                          True if SavedSearch was updated
     *                                  successfully, false otherwise
     */
    bool updateSavedSearch(
        SavedSearch & search, ErrorString & errorDescription);

    /**
     * @brief findSavedSearch attempts to find and fill the fields of passed in
     * saved search object.
     *
     * If "remote" Evernote services's guid for the saved search is set, it
     * would be used to identify the saved search in the local storage.
     * Otherwise the local uid would be used. If neither guid not local uid are
     * set, saved search's name would be used. If the name is also not set,
     * the search for saved search would fail.
     *
     * @param search                    SavedSearch to be found in the local
     *                                  storage database
     * @param errorDescription          Error description if SavedSearch could
     *                                  not be found
     * @return                          True if SavedSearch was found, false
     *                                  otherwise
     */
    bool findSavedSearch(
        SavedSearch & search, ErrorString & errorDescription) const;

    /**
     * @brief The ListSavedSearchesOrder enum allows to specify the results
     * ordering for methods listing saved searches from local storage
     */
    enum class ListSavedSearchesOrder
    {
        ByUpdateSequenceNumber = 0,
        ByName,
        ByFormat,
        NoOrder
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListSavedSearchesOrder order);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & strm, const ListSavedSearchesOrder order);

    /**
     * @brief listAllSavedSearches lists all saved searches within the account.
     * @param errorDescription          Error description if all saved searches
     *                                  could not be listed; otherwise this
     *                                  parameter is untouched
     * @param limit                     Limit for the max number of saved
     *                                  searches in the result, zero by default
     *                                  which means no limit is set
     * @param offset                    Number of saved searches to skip in the
     *                                  beginning of the result, zero by default
     * @param order                     Allows to specify particular ordering of
     *                                  saved searches in the result, NoOrder by
     *                                  default
     * @param orderDirection            Specifies the direction of ordering,
     *                                  by default ascending direction is used;
     *                                  this parameter has no meaning if order
     *                                  is equal to NoOrder
     * @return                          Either the list of all saved searches
     *                                  within the account or empty list in case
     *                                  of error or if there are no saved
     *                                  searches within the account
     */
    QList<SavedSearch> listAllSavedSearches(
        ErrorString & errorDescription, const size_t limit = 0,
        const size_t offset = 0,
        const ListSavedSearchesOrder order = ListSavedSearchesOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending) const;

    /**
     * @brief listSavedSearches attempts to list saved searches within
     * the account according to the specified input flag.
     *
     * @param flag                      Input parameter used to set the filter
     *                                  for the desired saved searches to be
     *                                  listed
     * @param errorDescription          Error description if saved searches
     *                                  within the account could not be listed;
     *                                  if no error happens, this parameter is
     *                                  untouched
     * @param limit                     Limit for the max number of saved
     *                                  searches in the result, zero by default
     *                                  which means no limit is set
     * @param offset                    Number of saved searches to skip in the
     *                                  beginning of the result, zero by default
     * @param order                     Allows to specify particular ordering of
     *                                  saved searches in the result, NoOrder
     *                                  by default
     * @param orderDirection            Specifies the direction of ordering,
     *                                  by default ascending direction is used;
     *                                  this parameter has no meaning if order
     *                                  is equal to NoOrder
     * @return                          Either list of saved searches within
     *                                  the account conforming to the filter or
     *                                  empty list in cases of error or no saved
     *                                  searches conforming to the filter exist
     *                                  within the account
     */
    QList<SavedSearch> listSavedSearches(
        const ListObjectsOptions flag, ErrorString & errorDescription,
        const size_t limit = 0, const size_t offset = 0,
        const ListSavedSearchesOrder order = ListSavedSearchesOrder::NoOrder,
        const OrderDirection orderDirection = OrderDirection::Ascending) const;

    /**
     * @brief expungeSavedSearch permanently deletes saved search from the local
     * storage database.
     *
     * @param search                    Saved search to be expunged; may be
     *                                  changed as a result of the call filled
     *                                  local uid if it was empty before
     *                                  the call
     * @param errorDescription          Error description if saved search could
     *                                  not be expunged
     * @return                          True if saved search was expunged
     *                                  successfully, false otherwise
     */
    bool expungeSavedSearch(
        SavedSearch & search, ErrorString & errorDescription);

    /**
     * @brief accountHighUsn returns the highest update sequence number within
     * the data elements stored in the local storage database, either for user's
     * own account or for some linked notebook.
     *
     * @param linkedNotebookGuid        The guid of the linked notebook for
     *                                  which the highest update sequence number
     *                                  is requested; if null or empty,
     *                                  the highest update sequence number for
     *                                  user's own account is returned
     * @param errorDescription          Error description if account's highest
     *                                  update sequence number could not be
     *                                  returned
     * @return                          Either the highest update sequence
     *                                  number - a non-negative value - or
     *                                  a negative number in case of error
     */
    qint32 accountHighUsn(
        const QString & linkedNotebookGuid, ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(LocalStorageManager)

    LocalStorageManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageManager)
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LocalStorageManager::GetNoteOptions)
Q_DECLARE_OPERATORS_FOR_FLAGS(LocalStorageManager::ListObjectsOptions)
Q_DECLARE_OPERATORS_FOR_FLAGS(LocalStorageManager::StartupOptions)
Q_DECLARE_OPERATORS_FOR_FLAGS(LocalStorageManager::UpdateNoteOptions)

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_H
