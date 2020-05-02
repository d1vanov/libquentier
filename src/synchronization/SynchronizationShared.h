/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SHARED_H
#define LIB_QUENTIER_SYNCHRONIZATION_SHARED_H

#include <quentier/utility/Printable.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

#include <QVector>
#include <QString>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#endif

#define SYNCHRONIZATION_PERSISTENCE_NAME                                       \
    QStringLiteral("SynchronizationPersistence")                               \
// SYNCHRONIZATION_PERSISTENCE_NAME

#define HALF_AN_HOUR_IN_MSEC (1800000)

#define AUTHENTICATION_TIMESTAMP_KEY QStringLiteral("AuthenticationTimestamp")

#define EXPIRATION_TIMESTAMP_KEY QStringLiteral("ExpirationTimestamp")

#define USER_STORE_COOKIE_KEY QStringLiteral("UserStoreCookie")

#define LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX                        \
    QStringLiteral("LinkedNotebookExpirationTimestamp_")                       \
// LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX

#define LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART                                    \
    QStringLiteral("_LinkedNotebookAuthToken_")                                \
// LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART

#define LINKED_NOTEBOOK_SHARD_ID_KEY_PART                                      \
    QStringLiteral("_LinkedNotebookShardId_")                                  \
// LINKED_NOTEBOOK_SHARD_ID_KEY_PART

#define READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB                                    \
    QStringLiteral("readLinkedNotebookAuthToken")                              \
// READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB

#define READ_LINKED_NOTEBOOK_SHARD_ID_JOB                                      \
    QStringLiteral("readLinkedNotebookShardId")                                \
// READ_LINKED_NOTEBOOK_SHARD_ID_JOB

#define WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB                                   \
    QStringLiteral("writeLinkedNotebookAuthToken")                             \
// WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB

#define WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB                                     \
    QStringLiteral("writeLinkedNotebookShardId")                               \
// WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB

#define NOTE_STORE_URL_KEY QStringLiteral("NoteStoreUrl")
#define WEB_API_URL_PREFIX_KEY QStringLiteral("WebApiUrlPrefix")

#define LAST_SYNC_PARAMS_KEY_GROUP QStringLiteral("last_sync_params")
#define LAST_SYNC_UPDATE_COUNT_KEY QStringLiteral("last_sync_update_count")
#define LAST_SYNC_TIME_KEY         QStringLiteral("last_sync_time")

#define LAST_SYNC_LINKED_NOTEBOOKS_PARAMS                                      \
    QStringLiteral("last_sync_linked_notebooks_params")                        \
// LAST_SYNC_LINKED_NOTEBOOKS_PARAMS

#define LINKED_NOTEBOOK_GUID_KEY QStringLiteral("linked_notebook_guid")

#define LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY                                  \
    QStringLiteral("linked_notebook_last_update_count")                        \
// LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY

#define LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY                                     \
    QStringLiteral("linked_notebook_last_sync_time")                           \
// LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY

#define AUTH_TOKEN_KEYCHAIN_KEY_PART QStringLiteral("_auth_token")
#define SHARD_ID_KEYCHAIN_KEY_PART QStringLiteral("_shard_id")

#define APPEND_NOTE_DETAILS(errorDescription, note)                            \
   if (note.hasTitle())                                                        \
   {                                                                           \
       errorDescription.details() = note.title();                              \
   }                                                                           \
   else if (note.hasContent())                                                 \
   {                                                                           \
       QString previewText = note.plainText();                                 \
       if (!previewText.isEmpty()) {                                           \
           previewText.truncate(30);                                           \
           errorDescription.details() = previewText;                           \
       }                                                                       \
   }                                                                           \
// APPEND_NOTE_DETAILS

namespace quentier {

class Q_DECL_HIDDEN LinkedNotebookAuthData: public Printable
{
public:
    LinkedNotebookAuthData();
    LinkedNotebookAuthData(const QString & guid,
                           const QString & shardId,
                           const QString & sharedNotebookGlobalId,
                           const QString & uri,
                           const QString & noteStoreUrl);

    virtual QTextStream & print(QTextStream & strm) const override;

    QString     m_guid;
    QString     m_shardId;
    QString     m_sharedNotebookGlobalId;
    QString     m_uri;
    QString     m_noteStoreUrl;
};

template <typename T>
class OptionalComparator
{
public:
    bool operator()(const qevercloud::Optional<T> & lhs,
                    const qevercloud::Optional<T> & rhs) const
    {
        if (!lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else if (!lhs.isSet() && rhs.isSet()) {
            return true;
        }
        else if (lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else {
            return lhs.ref() < rhs.ref();
        }
    }
};

class OptionalStringCaseInsensitiveComparator
{
public:
    bool operator()(const qevercloud::Optional<QString> & lhs,
                    const qevercloud::Optional<QString> & rhs) const
    {
        if (!lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else if (!lhs.isSet() && rhs.isSet()) {
            return true;
        }
        else if (lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else {
            return lhs.ref().toUpper() < rhs.ref().toUpper();
        }
    }
};

struct ByGuid{};
struct ByName{};
struct ByParentTagGuid{};

typedef boost::multi_index_container<
    qevercloud::Tag,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            boost::multi_index::tag<ByGuid>,
            boost::multi_index::member<
                qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::guid>,
            OptionalComparator<QString>
        >,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByName>,
            boost::multi_index::member<
                qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::name>,
            OptionalStringCaseInsensitiveComparator
        >,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByParentTagGuid>,
            boost::multi_index::member<
            qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::parentGuid>,
            OptionalComparator<QString>
        >
    >
> TagsContainer;

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SHARED_H
