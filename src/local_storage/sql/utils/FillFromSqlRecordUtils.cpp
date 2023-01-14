/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include "FillFromSqlRecordUtils.h"
#include "ListFromDatabaseUtils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/User.h>

#include <QGlobalStatic>

namespace quentier::local_storage::sql::utils {

namespace {

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingUserFieldErrorMessage,
    (QString::fromUtf8(
        "User field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingNotebookFieldErrorMessage,
    (QString::fromUtf8(
        "Notebook field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingTagFieldErrorMessage,
    (QString::fromUtf8(
        "Tag field missing in the record received from the local storage "
        "database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingLinkedNotebookFieldErrorMessage,
    (QString::fromUtf8(
        "LinkedNotebook field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingResourceFieldErrorMessage,
    (QString::fromUtf8(
        "Resource field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingSavedSearchFieldErrorMessage,
    (QString::fromUtf8(
        "Saved search field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingNoteFieldErrorMessage,
    (QString::fromUtf8(
        "Note field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingSharedNoteFieldErrorMessage,
    (QString::fromUtf8(
        "Shared note field missing in the record received from the local "
        "storage database")));

template <class Type, class VariantType, class LocalType = VariantType>
bool fillValue(
    const QSqlRecord & record, const QString & column, Type & typeValue,
    std::function<void(Type&, LocalType)> setter,
    const QString & missingFieldErrorDescription,
    ErrorString * errorDescription = nullptr)
{
    bool valueFound = false;
    const int index = record.indexOf(column);
    if (index >= 0) {
        const QVariant & value = record.value(column);
        if (!value.isNull()) {
            if constexpr (
                std::is_same_v<LocalType, VariantType> ||
                (std::is_convertible_v<VariantType, LocalType> &&
                 sizeof(LocalType) >= sizeof(VariantType)))
            {
                setter(
                    typeValue,
                    qvariant_cast<VariantType>(value));
            }
            else {
                setter(
                    typeValue,
                    static_cast<LocalType>(qvariant_cast<VariantType>(value)));
            }
            valueFound = true;
        }
    }

    if (!valueFound && errorDescription) {
        errorDescription->setBase(missingFieldErrorDescription);
        errorDescription->details() = column;
        QNWARNING("local_storage:sql:utils", *errorDescription);
        return false;
    }

    return valueFound;
}

template <class VariantType, class LocalType = VariantType>
bool fillUserValue(
    const QSqlRecord & record, const QString & column, qevercloud::User & user,
    std::function<void(qevercloud::User&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::User, VariantType, LocalType>(
        record, column, user, std::move(setter), *gMissingUserFieldErrorMessage,
        errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillNotebookValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::Notebook & notebook,
    std::function<void(qevercloud::Notebook&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::Notebook, VariantType, LocalType>(
        record, column, notebook, std::move(setter),
        *gMissingNotebookFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillSharedNotebookValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::SharedNotebook & sharedNotebook,
    std::function<void(qevercloud::SharedNotebook&, LocalType)> setter)
{
    return fillValue<qevercloud::SharedNotebook, VariantType, LocalType>(
        record, column, sharedNotebook, std::move(setter),
        *gMissingNotebookFieldErrorMessage);
}

template <class VariantType, class LocalType = VariantType>
bool fillTagValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::Tag & tag,
    std::function<void(qevercloud::Tag&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::Tag, VariantType, LocalType>(
        record, column, tag, std::move(setter),
        *gMissingTagFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillLinkedNotebookValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::LinkedNotebook & linkedNotebook,
    std::function<void(qevercloud::LinkedNotebook&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::LinkedNotebook, VariantType, LocalType>(
        record, column, linkedNotebook, std::move(setter),
        *gMissingLinkedNotebookFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillResourceValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::Resource & resource,
    std::function<void(qevercloud::Resource&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::Resource, VariantType, LocalType>(
        record, column, resource, std::move(setter),
        *gMissingResourceFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillResourceAttributeValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::Resource & resource,
    std::function<
        void(std::optional<qevercloud::ResourceAttributes> &, LocalType)>
        setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<
        std::optional<qevercloud::ResourceAttributes>, VariantType, LocalType>(
        record, column, resource.mutableAttributes(), std::move(setter),
        *gMissingResourceFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillSavedSearchValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::SavedSearch & savedSearch,
    std::function<void(qevercloud::SavedSearch&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::SavedSearch, VariantType, LocalType>(
        record, column, savedSearch, std::move(setter),
        *gMissingSavedSearchFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillNoteValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::Note & note,
    std::function<void(qevercloud::Note&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::Note, VariantType, LocalType>(
        record, column, note, std::move(setter),
        *gMissingNoteFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
bool fillSharedNoteValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::SharedNote & sharedNote,
    std::function<void(qevercloud::SharedNote&, LocalType)> setter,
    ErrorString * errorDescription = nullptr)
{
    return fillValue<qevercloud::SharedNote, VariantType, LocalType>(
        record, column, sharedNote, std::move(setter),
        *gMissingSharedNoteFieldErrorMessage, errorDescription);
}

template <class VariantType, class LocalType = VariantType>
void fillNoteAttributeValue(
    const QSqlRecord & record, const QString & column,
    qevercloud::NoteAttributes & attributes,
    std::function<void(qevercloud::NoteAttributes&, LocalType)> setter)
{
    fillValue<qevercloud::NoteAttributes, VariantType, LocalType>(
        record, column, attributes, std::move(setter), QString{});
}

template <class FieldType, class VariantType, class LocalType = VariantType>
void fillOptionalFieldValue(
    const QSqlRecord & record, const QString & column,
    std::optional<FieldType> & object,
    std::function<void(FieldType&, LocalType)> setter)
{
    const int index = record.indexOf(column);
    if (index < 0) {
        return;
    }

    const QVariant value = record.value(index);
    if (value.isNull()) {
        return;
    }

    if (!object) {
        object.emplace(FieldType{});
    }

    if constexpr (
        std::is_same_v<LocalType, VariantType> ||
        std::is_convertible_v<VariantType, LocalType>) {
        setter(
            *object,
            qvariant_cast<VariantType>(value));
    }
    else {
        setter(
            *object,
            static_cast<LocalType>(qvariant_cast<VariantType>(value)));
    }
}

template <class VariantType, class LocalType = VariantType>
void fillUserAttributeValue(
    const QSqlRecord & record, const QString & column,
    std::optional<qevercloud::UserAttributes> & userAttributes,
    std::function<void(qevercloud::UserAttributes&, LocalType)> setter)
{
    fillOptionalFieldValue<qevercloud::UserAttributes, VariantType, LocalType>(
        record, column, userAttributes, std::move(setter));
}

template <class VariantType, class LocalType = VariantType>
void fillAccountingValue(
    const QSqlRecord & record, const QString & column,
    std::optional<qevercloud::Accounting> & accounting,
    std::function<void(qevercloud::Accounting&, LocalType)> setter)
{
    fillOptionalFieldValue<qevercloud::Accounting, VariantType, LocalType>(
        record, column, accounting, std::move(setter));
}

template <class VariantType, class LocalType = VariantType>
void fillBusinessUserInfoValue(
    const QSqlRecord & record, const QString & column,
    std::optional<qevercloud::BusinessUserInfo> & businessUserInfo,
    std::function<void(qevercloud::BusinessUserInfo&, LocalType)> setter)
{
    fillOptionalFieldValue<qevercloud::BusinessUserInfo, VariantType, LocalType>(
        record, column, businessUserInfo, std::move(setter));
}

template <class VariantType, class LocalType = VariantType>
void fillAccountLimitsValue(
    const QSqlRecord & record, const QString & column,
    std::optional<qevercloud::AccountLimits> & accountLimits,
    std::function<void(qevercloud::AccountLimits&, LocalType)> setter)
{
    fillOptionalFieldValue<qevercloud::AccountLimits, VariantType, LocalType>(
        record, column, accountLimits, std::move(setter));
}

void fillNoteAttributesFromSqlRecord(
    const QSqlRecord & record, qevercloud::NoteAttributes & attributes)
{
    using qevercloud::NoteAttributes;

    fillNoteAttributeValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("subjectDate"), attributes,
        &NoteAttributes::setSubjectDate);

    fillNoteAttributeValue<double, double>(
        record, QStringLiteral("latitude"), attributes,
        &NoteAttributes::setLatitude);

    fillNoteAttributeValue<double, double>(
        record, QStringLiteral("longitude"), attributes,
        &NoteAttributes::setLongitude);

    fillNoteAttributeValue<double, double>(
        record, QStringLiteral("altitude"), attributes,
        &NoteAttributes::setAltitude);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("author"), attributes,
        &NoteAttributes::setAuthor);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("source"), attributes,
        &NoteAttributes::setSource);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("sourceURL"), attributes,
        &NoteAttributes::setSourceURL);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("sourceApplication"), attributes,
        &NoteAttributes::setSourceApplication);

    fillNoteAttributeValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("shareDate"), attributes,
        &NoteAttributes::setShareDate);

    fillNoteAttributeValue<qint64, qint64>(
        record, QStringLiteral("reminderOrder"), attributes,
        &NoteAttributes::setReminderOrder);

    fillNoteAttributeValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("reminderDoneTime"), attributes,
        &NoteAttributes::setReminderDoneTime);

    fillNoteAttributeValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("reminderTime"), attributes,
        &NoteAttributes::setReminderTime);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("placeName"), attributes,
        &NoteAttributes::setPlaceName);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("contentClass"), attributes,
        &NoteAttributes::setContentClass);

    fillNoteAttributeValue<QString, QString>(
        record, QStringLiteral("lastEditedBy"), attributes,
        &NoteAttributes::setLastEditedBy);

    fillNoteAttributeValue<qint32, qevercloud::UserID>(
        record, QStringLiteral("creatorId"), attributes,
        &NoteAttributes::setCreatorId);

    fillNoteAttributeValue<qint32, qevercloud::UserID>(
        record, QStringLiteral("lastEditorId"), attributes,
        &NoteAttributes::setLastEditorId);

    fillNoteAttributeValue<int, bool>(
        record, QStringLiteral("sharedWithBusiness"), attributes,
        &NoteAttributes::setSharedWithBusiness);

    fillNoteAttributeValue<QString, qevercloud::Guid>(
        record, QStringLiteral("conflictSourceNoteGuid"), attributes,
        &NoteAttributes::setConflictSourceNoteGuid);

    fillNoteAttributeValue<qint32, qint32>(
        record, QStringLiteral("noteTitleQuality"), attributes,
        &NoteAttributes::setNoteTitleQuality);
}

void fillNoteAttributesApplicationDataKeysOnlyFromSqlRecord(
    const QSqlRecord & record, qevercloud::NoteAttributes & attributes)
{
    const int keysOnlyIndex =
        record.indexOf(QStringLiteral("applicationDataKeysOnly"));
    if (keysOnlyIndex < 0) {
        return;
    }

    const QVariant value = record.value(keysOnlyIndex);
    if (value.isNull()) {
        return;
    }

    bool applicationDataWasEmpty = !attributes.applicationData();
    if (applicationDataWasEmpty) {
        attributes.setApplicationData(qevercloud::LazyMap{});
    }

    if (!attributes.applicationData()->keysOnly()) {
        attributes.mutableApplicationData()->setKeysOnly(QSet<QString>{});
    }

    QSet<QString> & keysOnly =
        *attributes.mutableApplicationData()->mutableKeysOnly();

    const QString keysOnlyString = value.toString();
    const int length = keysOnlyString.length();
    bool insideQuotedText = false;
    QString currentKey;
    const QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (length - 1); ++i) {
        const QChar currentChar = keysOnlyString.at(i);
        const QChar nextChar = keysOnlyString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysOnly.insert(currentKey);
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    if (!currentKey.isEmpty()) {
        keysOnly.insert(currentKey);
    }

    if (keysOnly.isEmpty()) {
        if (applicationDataWasEmpty) {
            attributes.mutableApplicationData() = std::nullopt;
        }
        else {
            attributes.mutableApplicationData()->mutableKeysOnly() =
                std::nullopt;
        }
    }
}

void fillNoteAttributesApplicationDataFullMapFromSqlRecord(
    const QSqlRecord & record, qevercloud::NoteAttributes & attributes)
{
    const int keyIndex = record.indexOf(QStringLiteral("applicationDataKeysMap"));
    const int valueIndex = record.indexOf(QStringLiteral("applicationDataValues"));
    if ((keyIndex < 0) || (valueIndex < 0)) {
        return;
    }

    const QVariant keys = record.value(keyIndex);
    const QVariant values = record.value(valueIndex);
    if (keys.isNull() || values.isNull()) {
        return;
    }

    const bool applicationDataWasEmpty = !attributes.applicationData();
    if (applicationDataWasEmpty) {
        attributes.setApplicationData(qevercloud::LazyMap{});
    }

    if (!attributes.applicationData()->fullMap()) {
        attributes.mutableApplicationData()->setFullMap(
            QMap<QString, QString>{});
    }

    QMap<QString, QString> & fullMap =
        *attributes.mutableApplicationData()->mutableFullMap();

    QStringList keysList, valuesList;

    const QString keysString = keys.toString();
    const int keysLength = keysString.length();
    keysList.reserve(keysLength / 2); // NOTE: just a wild guess

    bool insideQuotedText = false;
    QString currentKey;
    const QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (keysLength - 1); ++i) {
        const QChar currentChar = keysString.at(i);
        const QChar nextChar = keysString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysList << currentKey;
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    if (!currentKey.isEmpty()) {
        keysList << currentKey;
    }

    const QString valuesString = values.toString();
    int valuesLength = valuesString.length();
    valuesList.reserve(valuesLength / 2); // NOTE: just a wild guess

    insideQuotedText = false;
    QString currentValue;
    for (int i = 0; i < (valuesLength - 1); ++i) {
        const QChar currentChar = valuesString.at(i);
        const QChar nextChar = valuesString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                valuesList << currentValue;
                currentValue.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentValue.append(currentChar);
        }
    }

    if (!currentValue.isEmpty()) {
        valuesList << currentValue;
    }

    int numKeys = keysList.size();
    for (int i = 0; i < numKeys; ++i) {
        fullMap.insert(keysList.at(i), valuesList.at(i));
    }

    if (fullMap.isEmpty()) {
        if (applicationDataWasEmpty) {
            attributes.mutableApplicationData() = std::nullopt;
        }
        else {
            attributes.mutableApplicationData()->mutableFullMap() =
                std::nullopt;
        }
    }
}

void fillNoteAttributesClassificationsFromSqlRecord(
    const QSqlRecord & record, qevercloud::NoteAttributes & attributes)
{
    const int keyIndex = record.indexOf(QStringLiteral("classificationKeys"));
    const int valueIndex = record.indexOf(QStringLiteral("classificationValues"));
    if ((keyIndex < 0) || (valueIndex < 0)) {
        return;
    }

    const QVariant keys = record.value(keyIndex);
    const QVariant values = record.value(valueIndex);
    if (keys.isNull() || values.isNull()) {
        return;
    }

    const bool classificationsWereEmpty = !attributes.classifications();
    if (classificationsWereEmpty) {
        attributes.setClassifications(QMap<QString, QString>());
    }

    QMap<QString, QString> & classifications =
        *attributes.mutableClassifications();
    QStringList keysList, valuesList;

    const QString keysString = keys.toString();
    const int keysLength = keysString.length();
    keysList.reserve(keysLength / 2); // NOTE: just a wild guess
    bool insideQuotedText = false;
    QString currentKey;
    QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (keysLength - 1); ++i) {
        const QChar currentChar = keysString.at(i);
        const QChar nextChar = keysString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysList << currentKey;
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    const QString valuesString = values.toString();
    const int valuesLength = valuesString.length();
    valuesList.reserve(valuesLength / 2); // NOTE: just a wild guess

    insideQuotedText = false;
    QString currentValue;
    for (int i = 0; i < (valuesLength - 1); ++i) {
        const QChar currentChar = valuesString.at(i);
        const QChar nextChar = valuesString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                valuesList << currentValue;
                currentValue.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentValue.append(currentChar);
        }
    }

    int numKeys = keysList.size();
    for (int i = 0; i < numKeys; ++i) {
        classifications[keysList.at(i)] = valuesList.at(i);
    }

    if (classifications.isEmpty() && classificationsWereEmpty) {
        attributes.mutableClassifications() = std::nullopt;
    }
}

} // namespace

bool fillUserFromSqlRecord(
    const QSqlRecord & record, qevercloud::User & user,
    ErrorString & errorDescription)
{
    using qevercloud::User;

    if (!fillUserValue<int, bool>(
            record, QStringLiteral("userIsDirty"), user,
            &User::setLocallyModified, &errorDescription))
    {
        return false;
    }

    if (!fillUserValue<int, bool>(
            record, QStringLiteral("userIsLocal"), user,
            &User::setLocalOnly, &errorDescription))
    {
        return false;
    }

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(User &, std::optional<QString>)> setter) {
            fillUserValue<QString, std::optional<QString>>(
                record, column, user, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("username"), &User::setUsername);
    fillOptStringValue(QStringLiteral("email"), &User::setEmail);
    fillOptStringValue(QStringLiteral("name"), &User::setName);
    fillOptStringValue(QStringLiteral("timezone"), &User::setTimezone);
    fillOptStringValue(QStringLiteral("userShardId"), &User::setShardId);
    fillOptStringValue(QStringLiteral("photoUrl"), &User::setPhotoUrl);

    fillUserValue<int, qevercloud::PrivilegeLevel>(
        record, QStringLiteral("privilege"), user, &User::setPrivilege);

    const auto fillOptTimestampValue =
        [&](const QString & column,
            std::function<void(User &, qevercloud::Timestamp)> setter) {
            fillUserValue<qint64, qevercloud::Timestamp>(
                record, column, user, std::move(setter));
        };

    fillOptTimestampValue(
        QStringLiteral("userCreationTimestamp"), &User::setCreated);

    fillOptTimestampValue(
        QStringLiteral("userModificationTimestamp"), &User::setUpdated);

    fillOptTimestampValue(
        QStringLiteral("userDeletionTimestamp"), &User::setDeleted);

    fillOptTimestampValue(
        QStringLiteral("photoLastUpdated"), &User::setPhotoLastUpdated);

    fillUserValue<int, bool>(
        record, QStringLiteral("userIsActive"), user, &User::setActive);

    std::optional<qevercloud::UserAttributes> userAttributes;
    fillUserAttributesFromSqlRecord(record, userAttributes);
    user.setAttributes(std::move(userAttributes));

    std::optional<qevercloud::Accounting> accounting;
    fillAccountingFromSqlRecord(record, accounting);
    user.setAccounting(std::move(accounting));

    std::optional<qevercloud::BusinessUserInfo> businessUserInfo;
    fillBusinessUserInfoFromSqlRecord(record, businessUserInfo);
    user.setBusinessUserInfo(std::move(businessUserInfo));

    std::optional<qevercloud::AccountLimits> accountLimits;
    fillAccountLimitsFromSqlRecord(record, accountLimits);
    user.setAccountLimits(std::move(accountLimits));

    return true;
}

void fillUserAttributesFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::UserAttributes> & userAttributes)
{
    using qevercloud::UserAttributes;

    const auto fillStringValue =
        [&](const QString & column,
            std::function<void(UserAttributes &, std::optional<QString>)>
                setter) {
            fillUserAttributeValue<QString, std::optional<QString>>(
                record, column, userAttributes, std::move(setter));
        };

    fillStringValue(
        QStringLiteral("defaultLocationName"),
        &UserAttributes::setDefaultLocationName);

    fillStringValue(
        QStringLiteral("incomingEmailAddress"),
        &UserAttributes::setIncomingEmailAddress);

    fillStringValue(
        QStringLiteral("comments"), &UserAttributes::setComments);

    fillStringValue(
        QStringLiteral("refererCode"), &UserAttributes::setRefererCode);

    fillStringValue(
        QStringLiteral("preferredLanguage"),
        &UserAttributes::setPreferredLanguage);

    fillStringValue(
        QStringLiteral("preferredCountry"),
        &UserAttributes::setPreferredCountry);

    fillStringValue(
        QStringLiteral("twitterUserName"),
        &UserAttributes::setTwitterUserName);

    fillStringValue(
        QStringLiteral("twitterId"), &UserAttributes::setTwitterId);

    fillStringValue(
        QStringLiteral("groupName"), &UserAttributes::setGroupName);

    fillStringValue(
        QStringLiteral("recognitionLanguage"),
        &UserAttributes::setRecognitionLanguage);

    fillStringValue(
        QStringLiteral("referralProof"), &UserAttributes::setReferralProof);

    fillStringValue(
        QStringLiteral("businessAddress"), &UserAttributes::setBusinessAddress);

    const auto fillDoubleValue =
        [&](const QString & column,
            std::function<void(UserAttributes &, std::optional<double>)>
                setter) {
            fillUserAttributeValue<double, std::optional<double>>(
                record, column, userAttributes, std::move(setter));
        };

    fillDoubleValue(
        QStringLiteral("defaultLatitude"), &UserAttributes::setDefaultLatitude);

    fillDoubleValue(
        QStringLiteral("defaultLongitude"),
        &UserAttributes::setDefaultLongitude);

    const auto fillBoolValue =
        [&](const QString & column,
            std::function<void(UserAttributes &, std::optional<bool>)> setter) {
            fillUserAttributeValue<int, std::optional<bool>>(
                record, column, userAttributes, std::move(setter));
        };

    fillBoolValue(
        QStringLiteral("preactivation"), &UserAttributes::setPreactivation);

    fillBoolValue(
        QStringLiteral("clipFullPage"), &UserAttributes::setClipFullPage);

    fillBoolValue(
        QStringLiteral("educationalDiscount"),
        &UserAttributes::setEducationalDiscount);

    fillBoolValue(
        QStringLiteral("hideSponsorBilling"),
        &UserAttributes::setHideSponsorBilling);

    fillBoolValue(
        QStringLiteral("useEmailAutoFiling"),
        &UserAttributes::setUseEmailAutoFiling);

    fillBoolValue(
        QStringLiteral("salesforcePushEnabled"),
        &UserAttributes::setSalesforcePushEnabled);

    fillBoolValue(
        QStringLiteral("shouldLogClientEvent"),
        &UserAttributes::setShouldLogClientEvent);

    const auto fillTimestampValue =
        [&](const QString & column,
            std::function<void(
                UserAttributes &, std::optional<qevercloud::Timestamp>)>
                setter) {
            fillUserAttributeValue<
                qint64, std::optional<qevercloud::Timestamp>>(
                record, column, userAttributes, std::move(setter));
        };

    fillTimestampValue(
        QStringLiteral("dateAgreedToTermsOfService"),
        &UserAttributes::setDateAgreedToTermsOfService);

    fillTimestampValue(
        QStringLiteral("sentEmailDate"), &UserAttributes::setSentEmailDate);

    fillTimestampValue(
        QStringLiteral("emailOptOutDate"), &UserAttributes::setEmailOptOutDate);

    fillTimestampValue(
        QStringLiteral("partnerEmailOptInDate"),
        &UserAttributes::setPartnerEmailOptInDate);

    fillTimestampValue(
        QStringLiteral("emailAddressLastConfirmed"),
        &UserAttributes::setEmailAddressLastConfirmed);

    fillTimestampValue(
        QStringLiteral("passwordUpdated"), &UserAttributes::setPasswordUpdated);

    const auto fillIntValue =
        [&](const QString & column,
            std::function<void(UserAttributes &, std::optional<qint32>)>
                setter) {
            fillUserAttributeValue<qint32, std::optional<qint32>>(
                record, column, userAttributes, std::move(setter));
        };

    fillIntValue(
        QStringLiteral("maxReferrals"), &UserAttributes::setMaxReferrals);

    fillIntValue(
        QStringLiteral("referralCount"), &UserAttributes::setReferralCount);

    fillIntValue(
        QStringLiteral("sentEmailCount"), &UserAttributes::setSentEmailCount);

    fillIntValue(
        QStringLiteral("dailyEmailLimit"), &UserAttributes::setDailyEmailLimit);

    fillUserAttributeValue<qint32, qevercloud::ReminderEmailConfig>(
        record, QStringLiteral("reminderEmailConfig"), userAttributes,
        &UserAttributes::setReminderEmailConfig);
}

void fillAccountingFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::Accounting> & accounting)
{
    using qevercloud::Accounting;

    const auto fillStringValue =
        [&](const QString & column,
            std::function<void(Accounting &, std::optional<QString>)> setter) {
            fillAccountingValue<QString, std::optional<QString>>(
                record, column, accounting, std::move(setter));
        };

    fillStringValue(
        QStringLiteral("premiumOrderNumber"),
        &Accounting::setPremiumOrderNumber);

    fillStringValue(
        QStringLiteral("premiumCommerceService"),
        &Accounting::setPremiumCommerceService);

    fillStringValue(
        QStringLiteral("premiumServiceSKU"), &Accounting::setPremiumServiceSKU);

    fillStringValue(
        QStringLiteral("lastFailedChargeReason"),
        &Accounting::setLastFailedChargeReason);

    fillStringValue(
        QStringLiteral("premiumSubscriptionNumber"),
        &Accounting::setPremiumSubscriptionNumber);

    fillStringValue(
        QStringLiteral("currency"), &Accounting::setCurrency);

    const auto fillTimestampValue =
        [&](const QString & column,
            std::function<void(
                Accounting &, std::optional<qevercloud::Timestamp>)>
                setter) {
            fillAccountingValue<
                qint64, std::optional<qevercloud::Timestamp>>(
                record, column, accounting, std::move(setter));
        };

    fillTimestampValue(
        QStringLiteral("uploadLimitEnd"), &Accounting::setUploadLimitEnd);

    fillTimestampValue(
        QStringLiteral("premiumServiceStart"),
        &Accounting::setPremiumServiceStart);

    fillTimestampValue(
        QStringLiteral("lastSuccessfulCharge"),
        &Accounting::setLastSuccessfulCharge);

    fillTimestampValue(
        QStringLiteral("lastFailedCharge"), &Accounting::setLastFailedCharge);

    fillTimestampValue(
        QStringLiteral("nextPaymentDue"), &Accounting::setNextPaymentDue);

    fillTimestampValue(
        QStringLiteral("premiumLockUntil"), &Accounting::setPremiumLockUntil);

    fillTimestampValue(QStringLiteral("updated"), &Accounting::setUpdated);

    fillTimestampValue(
        QStringLiteral("lastRequestedCharge"),
        &Accounting::setLastRequestedCharge);

    fillTimestampValue(
        QStringLiteral("nextChargeDate"), &Accounting::setNextChargeDate);

    fillAccountingValue<qint64, qint64>(
        record, QStringLiteral("uploadLimitNextMonth"), accounting,
        &Accounting::setUploadLimitNextMonth);

    fillAccountingValue<int, qevercloud::PremiumOrderStatus>(
        record, QStringLiteral("premiumServiceStatus"), accounting,
        &Accounting::setPremiumServiceStatus);

    fillAccountingValue<int, qint32>(
        record, QStringLiteral("unitPrice"), accounting,
        &Accounting::setUnitPrice);

    fillAccountingValue<int, qint32>(
        record, QStringLiteral("unitDiscount"), accounting,
        &Accounting::setUnitDiscount);

    fillAccountingValue<int, qint32>(
        record, QStringLiteral("availablePoints"), accounting,
        &Accounting::setAvailablePoints);
}

void fillBusinessUserInfoFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::BusinessUserInfo> & businessUserInfo)
{
    using qevercloud::BusinessUserInfo;

    fillBusinessUserInfoValue<qint32, qint32>(
        record, QStringLiteral("businessId"), businessUserInfo,
        &BusinessUserInfo::setBusinessId);

    fillBusinessUserInfoValue<QString, QString>(
        record, QStringLiteral("businessName"), businessUserInfo,
        &BusinessUserInfo::setBusinessName);

    fillBusinessUserInfoValue<int, qevercloud::BusinessUserRole>(
        record, QStringLiteral("role"), businessUserInfo,
        &BusinessUserInfo::setRole);

    fillBusinessUserInfoValue<QString, QString>(
        record, QStringLiteral("businessInfoEmail"), businessUserInfo,
        &BusinessUserInfo::setEmail);
}

void fillAccountLimitsFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::AccountLimits> & accountLimits)
{
    using qevercloud::AccountLimits;

    const auto fillInt64Value =
        [&](const QString & column,
            std::function<void(AccountLimits &, std::optional<qint64>)>
                setter) {
            fillAccountLimitsValue<qint64, std::optional<qint64>>(
                record, column, accountLimits, std::move(setter));
        };

    fillInt64Value(
        QStringLiteral("noteSizeMax"), &AccountLimits::setNoteSizeMax);

    fillInt64Value(
        QStringLiteral("resourceSizeMax"), &AccountLimits::setResourceSizeMax);

    fillInt64Value(
        QStringLiteral("uploadLimit"), &AccountLimits::setUploadLimit);

    const auto fillInt32Value =
        [&](const QString & column,
            std::function<void(AccountLimits &, std::optional<qint32>)>
                setter) {
            fillAccountLimitsValue<int, std::optional<qint32>>(
                record, column, accountLimits, std::move(setter));
        };

    fillInt32Value(
        QStringLiteral("userMailLimitDaily"),
        &AccountLimits::setUserMailLimitDaily);

    fillInt32Value(
        QStringLiteral("userLinkedNotebookMax"),
        &AccountLimits::setUserLinkedNotebookMax);

    fillInt32Value(
        QStringLiteral("userNoteCountMax"),
        &AccountLimits::setUserNoteCountMax);

    fillInt32Value(
        QStringLiteral("userNotebookCountMax"),
        &AccountLimits::setUserNotebookCountMax);

    fillInt32Value(
        QStringLiteral("userTagCountMax"), &AccountLimits::setUserTagCountMax);

    fillInt32Value(
        QStringLiteral("noteTagCountMax"), &AccountLimits::setNoteTagCountMax);

    fillInt32Value(
        QStringLiteral("userSavedSearchesMax"),
        &AccountLimits::setUserSavedSearchesMax);

    fillInt32Value(
        QStringLiteral("noteResourceCountMax"),
        &AccountLimits::setNoteResourceCountMax);
}

bool fillNotebookFromSqlRecord(
    const QSqlRecord & record, qevercloud::Notebook & notebook,
    ErrorString & errorDescription)
{
    using qevercloud::BusinessNotebook;
    using qevercloud::Notebook;
    using qevercloud::Publishing;
    using qevercloud::NotebookRecipientSettings;
    using qevercloud::NotebookRestrictions;

    if (!fillNotebookValue<int, bool>(
            record, QStringLiteral("isDirty"), notebook,
            &Notebook::setLocallyModified, &errorDescription))
    {
        return false;
    }

    if (!fillNotebookValue<int, bool>(
            record, QStringLiteral("isLocal"), notebook,
            &Notebook::setLocalOnly, &errorDescription))
    {
        return false;
    }

    if (!fillNotebookValue<QString, QString>(
            record, QStringLiteral("localUid"), notebook,
            &Notebook::setLocalId, &errorDescription))
    {
        return false;
    }

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(Notebook &, std::optional<QString>)> setter) {
            fillNotebookValue<QString, std::optional<QString>>(
                record, column, notebook, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("notebookName"), &Notebook::setName);
    fillOptStringValue(QStringLiteral("guid"), &Notebook::setGuid);
    fillOptStringValue(QStringLiteral("stack"), &Notebook::setStack);

    fillOptStringValue(
        QStringLiteral("linkedNotebookGuid"), &Notebook::setLinkedNotebookGuid);

    const auto fillOptTimestampValue =
        [&](const QString & column,
            std::function<void(Notebook &, qevercloud::Timestamp)> setter) {
            fillNotebookValue<qint64, qevercloud::Timestamp>(
                record, column, notebook, std::move(setter));
        };

    fillOptTimestampValue(
        QStringLiteral("creationTimestamp"), &Notebook::setServiceCreated);

    fillOptTimestampValue(
        QStringLiteral("modificationTimestamp"), &Notebook::setServiceUpdated);

    const auto fillOptBoolValue =
        [&](const QString & column,
            std::function<void(Notebook &, bool)> setter) {
            fillNotebookValue<int, bool>(
                record, column, notebook, std::move(setter));
        };

    fillOptBoolValue(
        QStringLiteral("isFavorited"), &Notebook::setLocallyFavorited);

    fillOptBoolValue(
        QStringLiteral("isDefault"), &Notebook::setDefaultNotebook);

    fillOptBoolValue(
        QStringLiteral("isPublished"), &Notebook::setPublished);

    fillNotebookValue<qint32, qint32>(
        record, QStringLiteral("updateSequenceNumber"), notebook,
        &Notebook::setUpdateSequenceNum);

    const auto setNotebookPublishingValue =
        [](Notebook & notebook, auto setter)
        {
            if (!notebook.publishing()) {
                notebook.setPublishing(Publishing{});
            }
            setter(*notebook.mutablePublishing());
        };

    fillOptStringValue(
        QStringLiteral("publishingUri"),
        [&](Notebook & notebook, std::optional<QString> value)
        {
            setNotebookPublishingValue(
                notebook,
                [&value](Publishing & publishing)
                {
                    publishing.setUri(std::move(value));
                });
        });

    fillOptStringValue(
        QStringLiteral("publicDescription"),
        [&](Notebook & notebook, std::optional<QString> value)
        {
            setNotebookPublishingValue(
                notebook,
                [&value](Publishing & publishing)
                {
                    publishing.setPublicDescription(std::move(value));
                });
        });

    fillNotebookValue<int, qevercloud::NoteSortOrder>(
        record, QStringLiteral("publishingNoteSortOrder"), notebook,
        [&](Notebook & notebook, std::optional<qevercloud::NoteSortOrder> order)
        {
            setNotebookPublishingValue(
                notebook,
                [order](Publishing & publishing)
                {
                    publishing.setOrder(order);
                });
        });

    fillNotebookValue<int, bool>(
        record, QStringLiteral("publishingAscendingSort"), notebook,
        [&](Notebook & notebook, std::optional<bool> ascendingSort)
        {
            setNotebookPublishingValue(
                notebook,
                [ascendingSort](Publishing & publishing)
                {
                    publishing.setAscending(ascendingSort);
                });
        });

    const auto setBusinessNotebookValue =
        [](Notebook & notebook, auto setter)
        {
            if (!notebook.businessNotebook()) {
                notebook.setBusinessNotebook(BusinessNotebook{});
            }
            setter(*notebook.mutableBusinessNotebook());
        };

    fillOptStringValue(
        QStringLiteral("businessNotebookDescription"),
        [&](Notebook & notebook, std::optional<QString> value)
        {
            setBusinessNotebookValue(
                notebook,
                [&value](BusinessNotebook & businessNotebook)
                {
                    businessNotebook.setNotebookDescription(std::move(value));
                });
        });

    fillNotebookValue<int, qevercloud::SharedNotebookPrivilegeLevel>(
        record, QStringLiteral("businessNotebookPrivilegeLevel"), notebook,
        [&](Notebook & notebook,
            std::optional<qevercloud::SharedNotebookPrivilegeLevel> level)
        {
            setBusinessNotebookValue(
                notebook,
                [level](BusinessNotebook & businessNotebook)
                {
                    businessNotebook.setPrivilege(level);
                });
        });

    fillNotebookValue<int, bool>(
        record, QStringLiteral("businessNotebookIsRecommended"), notebook,
        [&](Notebook & notebook, std::optional<bool> recommended)
        {
            setBusinessNotebookValue(
                notebook,
                [recommended](BusinessNotebook & businessNotebook)
                {
                    businessNotebook.setRecommended(recommended);
                });
        });

    const auto setNotebookRecipientSettingValue =
        [](Notebook & notebook, auto setter)
        {
            if (!notebook.recipientSettings()) {
                notebook.setRecipientSettings(NotebookRecipientSettings{});
            }
            setter(*notebook.mutableRecipientSettings());
        };

    fillOptStringValue(
        QStringLiteral("recipientStack"),
        [&](Notebook & notebook, std::optional<QString> value)
        {
            setNotebookRecipientSettingValue(
                notebook,
                [&value](NotebookRecipientSettings & settings)
                {
                    settings.setStack(std::move(value));
                });
        });

    fillNotebookValue<int, bool>(
        record, QStringLiteral("recipientReminderNotifyEmail"), notebook,
        [&](Notebook & notebook, std::optional<bool> value)
        {
            setNotebookRecipientSettingValue(
                notebook,
                [value](NotebookRecipientSettings & settings)
                {
                    settings.setReminderNotifyEmail(value);
                });
        });

    fillNotebookValue<int, bool>(
        record, QStringLiteral("recipientReminderNotifyInApp"), notebook,
        [&](Notebook & notebook, std::optional<bool> value)
        {
            setNotebookRecipientSettingValue(
                notebook,
                [value](NotebookRecipientSettings & settings)
                {
                    settings.setReminderNotifyInApp(value);
                });
        });

    fillNotebookValue<int, bool>(
        record, QStringLiteral("recipientInMyList"), notebook,
        [&](Notebook & notebook, std::optional<bool> value)
        {
            setNotebookRecipientSettingValue(
                notebook,
                [value](NotebookRecipientSettings & settings)
                {
                    settings.setInMyList(value);
                });
        });

    if (record.contains(QStringLiteral("contactId")) &&
        !record.isNull(QStringLiteral("contactId")))
    {
        if (notebook.contact()) {
            auto & contact = *notebook.mutableContact();
            contact.setId(qvariant_cast<qint32>(
                record.value(QStringLiteral("contactId"))));
        }
        else {
            qevercloud::User contact;
            contact.setId(qvariant_cast<qint32>(
                record.value(QStringLiteral("contactId"))));
            notebook.setContact(contact);
        }

        auto & user = *notebook.mutableContact();
        if (!fillUserFromSqlRecord(record, user, errorDescription)) {
            return false;
        }
    }

    const auto setNotebookRestrictionValue =
        [](Notebook & notebook, auto setter)
        {
            if (!notebook.restrictions()) {
                notebook.setRestrictions(NotebookRestrictions{});
            }
            setter(*notebook.mutableRestrictions());
        };

    const auto fillNotebookRestrictionBoolValue =
        [&](const QString & column, auto setter)
        {
            fillNotebookValue<int, bool>(
                record, column, notebook,
                [&](Notebook & notebook, std::optional<bool> value)
                {
                    setNotebookRestrictionValue(
                        notebook,
                        [value, setter = std::move(setter)]
                        (NotebookRestrictions & restrictions)
                        {
                            setter(restrictions, value);
                        });
                });
        };

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noReadNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoReadNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noCreateNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoCreateNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noUpdateNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoUpdateNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noExpungeNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoExpungeNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noShareNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoShareNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noEmailNotes"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoEmailNotes(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noSendMessageToRecipients"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoSendMessageToRecipients(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noUpdateNotebook"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoUpdateNotebook(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noExpungeNotebook"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoExpungeNotebook(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noSetDefaultNotebook"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoSetDefaultNotebook(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noSetNotebookStack"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoSetNotebookStack(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noPublishToPublic"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoPublishToPublic(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noPublishToBusinessLibrary"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoPublishToBusinessLibrary(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noCreateTags"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoCreateTags(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noUpdateTags"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoUpdateTags(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noExpungeTags"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoExpungeTags(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noSetParentTag"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoSetParentTag(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noCreateSharedNotebooks"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoCreateSharedNotebooks(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noShareNotesWithBusiness"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoShareNotesWithBusiness(value);
        });

    fillNotebookRestrictionBoolValue(
        QStringLiteral("noRenameNotebook"),
        [](NotebookRestrictions & restrictions, std::optional<bool> value)
        {
            restrictions.setNoRenameNotebook(value);
        });

    fillNotebookValue<int, qevercloud::SharedNotebookInstanceRestrictions>(
        record, QStringLiteral("updateWhichSharedNotebookRestrictions"),
        notebook,
        [&](Notebook & notebook,
            std::optional<qevercloud::SharedNotebookInstanceRestrictions> value)
        {
            setNotebookRestrictionValue(
                notebook,
                [value](NotebookRestrictions & restrictions)
                {
                    restrictions.setUpdateWhichSharedNotebookRestrictions(
                        value);
                });
        });

    fillNotebookValue<int, qevercloud::SharedNotebookInstanceRestrictions>(
        record, QStringLiteral("expungeWhichSharedNotebookRestrictions"),
        notebook,
        [&](Notebook & notebook,
            std::optional<qevercloud::SharedNotebookInstanceRestrictions> value)
        {
            setNotebookRestrictionValue(
                notebook,
                [value](NotebookRestrictions & restrictions)
                {
                    restrictions.setExpungeWhichSharedNotebookRestrictions(
                        value);
                });
        });

    return true;
}

bool fillSharedNotebookFromSqlRecord(
    const QSqlRecord & record, qevercloud::SharedNotebook & sharedNotebook,
    int & indexInNotebook, ErrorString & errorDescription)
{
    using qevercloud::SharedNotebook;

    fillSharedNotebookValue<qint64, qint64>(
        record, QStringLiteral("sharedNotebookShareId"), sharedNotebook,
        &SharedNotebook::setId);

    fillSharedNotebookValue<qint32, qint32>(
        record, QStringLiteral("sharedNotebookUserId"), sharedNotebook,
        &SharedNotebook::setUserId);

    fillSharedNotebookValue<QString, QString>(
        record, QStringLiteral("sharedNotebookNotebookGuid"), sharedNotebook,
        &SharedNotebook::setNotebookGuid);

    fillSharedNotebookValue<QString, QString>(
        record, QStringLiteral("sharedNotebookEmail"), sharedNotebook,
        &SharedNotebook::setEmail);

    fillSharedNotebookValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNotebookCreationTimestamp"),
        sharedNotebook, &SharedNotebook::setServiceCreated);

    fillSharedNotebookValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNotebookModificationTimestamp"),
        sharedNotebook, &SharedNotebook::setServiceUpdated);

    fillSharedNotebookValue<QString, QString>(
        record, QStringLiteral("sharedNotebookGlobalId"), sharedNotebook,
        &SharedNotebook::setGlobalId);

    fillSharedNotebookValue<QString, QString>(
        record, QStringLiteral("sharedNotebookUsername"), sharedNotebook,
        &SharedNotebook::setUsername);

    fillSharedNotebookValue<int, qevercloud::SharedNotebookPrivilegeLevel>(
        record, QStringLiteral("sharedNotebookPrivilegeLevel"), sharedNotebook,
        &SharedNotebook::setPrivilege);

    fillSharedNotebookValue<qint32, qint32>(
        record, QStringLiteral("sharedNotebookSharerUserId"), sharedNotebook,
        &SharedNotebook::setSharerUserId);

    fillSharedNotebookValue<QString, QString>(
        record, QStringLiteral("sharedNotebookRecipientUsername"),
        sharedNotebook, &SharedNotebook::setRecipientUsername);

    fillSharedNotebookValue<qint32, qint32>(
        record, QStringLiteral("sharedNotebookRecipientUserId"), sharedNotebook,
        &SharedNotebook::setRecipientUserId);

    fillSharedNotebookValue<qint64, qint64>(
        record, QStringLiteral("sharedNotebookRecipientIdentityId"),
        sharedNotebook, &SharedNotebook::setRecipientIdentityId);

    fillSharedNotebookValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNotebookAssignmentTimestamp"),
        sharedNotebook, &SharedNotebook::setServiceAssigned);

    fillOptionalFieldValue<qevercloud::SharedNotebookRecipientSettings, int, bool>(
        record, QStringLiteral("sharedNotebookRecipientReminderNotifyEmail"),
        sharedNotebook.mutableRecipientSettings(),
        [](qevercloud::SharedNotebookRecipientSettings & settings, bool value)
        {
            settings.setReminderNotifyEmail(value);
        });

    fillOptionalFieldValue<qevercloud::SharedNotebookRecipientSettings, int, bool>(
        record, QStringLiteral("sharedNotebookRecipientReminderNotifyInApp"),
        sharedNotebook.mutableRecipientSettings(),
        [](qevercloud::SharedNotebookRecipientSettings & settings, bool value)
        {
            settings.setReminderNotifyInApp(value);
        });

    const int recordIndex = record.indexOf(QStringLiteral("indexInNotebook"));
    if (recordIndex >= 0) {
        const QVariant value = record.value(recordIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int index = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(QStringLiteral(
                    "cannot convert shared notebook's index in notebook to "
                    "int"));
                QNERROR("local_storage::sql::utils", errorDescription);
                return false;
            }
            indexInNotebook = index;
        }
    }

    return true;
}

bool fillTagFromSqlRecord(
    const QSqlRecord & record, qevercloud::Tag & tag,
    ErrorString & errorDescription)
{
    using qevercloud::Tag;

    if (!fillTagValue<QString, QString>(
            record, QStringLiteral("localUid"), tag,
            &Tag::setLocalId, &errorDescription))
    {
        return false;
    }

    if (!fillTagValue<int, bool>(
            record, QStringLiteral("isDirty"), tag,
            &Tag::setLocallyModified, &errorDescription))
    {
        return false;
    }

    if (!fillTagValue<int, bool>(
            record, QStringLiteral("isLocal"), tag,
            &Tag::setLocalOnly, &errorDescription))
    {
        return false;
    }

    if (!fillTagValue<int, bool>(
            record, QStringLiteral("isFavorited"), tag,
            &Tag::setLocallyFavorited, &errorDescription))
    {
        return false;
    }

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(Tag&, std::optional<QString>)> setter) {
            fillTagValue<QString, std::optional<QString>>(
                record, column, tag, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("guid"), &Tag::setGuid);
    fillOptStringValue(QStringLiteral("name"), &Tag::setName);
    fillOptStringValue(QStringLiteral("parentGuid"), &Tag::setParentGuid);

    fillOptStringValue(
        QStringLiteral("linkedNotebookGuid"), &Tag::setLinkedNotebookGuid);

    fillTagValue<QString, QString>(
        record, QStringLiteral("parentLocalUid"), tag,
        &Tag::setParentTagLocalId);

    fillTagValue<qint32, qint32>(
        record, QStringLiteral("updateSequenceNumber"), tag,
        &Tag::setUpdateSequenceNum);

    return true;
}

bool fillLinkedNotebookFromSqlRecord(
    const QSqlRecord & record, qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    using qevercloud::LinkedNotebook;

    if (!fillLinkedNotebookValue<QString, QString>(
            record, QStringLiteral("guid"), linkedNotebook,
            &LinkedNotebook::setGuid, &errorDescription))
    {
        return false;
    }

    if (!fillLinkedNotebookValue<int, bool>(
            record, QStringLiteral("isDirty"), linkedNotebook,
            &LinkedNotebook::setLocallyModified, &errorDescription))
    {
        return false;
    }

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(LinkedNotebook &, std::optional<QString>)>
                setter) {
            fillLinkedNotebookValue<QString, std::optional<QString>>(
                record, column, linkedNotebook, std::move(setter));
        };

    fillOptStringValue(
        QStringLiteral("shareName"), &LinkedNotebook::setShareName);

    fillOptStringValue(
        QStringLiteral("username"), &LinkedNotebook::setUsername);

    fillOptStringValue(QStringLiteral("shardId"), &LinkedNotebook::setShardId);
    fillOptStringValue(QStringLiteral("uri"), &LinkedNotebook::setUri);
    fillOptStringValue(QStringLiteral("stack"), &LinkedNotebook::setStack);

    fillOptStringValue(
        QStringLiteral("noteStoreUrl"), &LinkedNotebook::setNoteStoreUrl);

    fillOptStringValue(
        QStringLiteral("webApiUrlPrefix"), &LinkedNotebook::setWebApiUrlPrefix);

    fillOptStringValue(
        QStringLiteral("sharedNotebookGlobalId"),
        &LinkedNotebook::setSharedNotebookGlobalId);

    fillLinkedNotebookValue<qint32, qint32>(
        record, QStringLiteral("updateSequenceNumber"), linkedNotebook,
        &LinkedNotebook::setUpdateSequenceNum);

    fillLinkedNotebookValue<qint32, qint32>(
        record, QStringLiteral("businessId"), linkedNotebook,
        &LinkedNotebook::setBusinessId);

    return true;
}

bool fillResourceFromSqlRecord(
    const QSqlRecord & record, qevercloud::Resource & resource,
    int & indexInNote, ErrorString & errorDescription)
{
    using qevercloud::Resource;

    if (!fillResourceValue<QString, QString>(
            record, QStringLiteral("resourceLocalUid"), resource,
            &Resource::setLocalId, &errorDescription))
    {
        return false;
    }

    if (!fillResourceValue<int, bool>(
            record, QStringLiteral("resourceIsDirty"), resource,
            &Resource::setLocallyModified, &errorDescription))
    {
        return false;
    }

    fillResourceValue<QString, QString>(
        record, QStringLiteral("localNote"), resource,
        &Resource::setNoteLocalId);

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(Resource &, std::optional<QString>)> setter) {
            fillResourceValue<QString, std::optional<QString>>(
                record, column, resource, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("noteGuid"), &Resource::setNoteGuid);
    fillOptStringValue(QStringLiteral("mime"), &Resource::setMime);
    fillOptStringValue(QStringLiteral("resourceGuid"), &Resource::setGuid);

    fillResourceValue<int, qint32>(
        record, QStringLiteral("resourceUpdateSequenceNumber"), resource,
        &Resource::setUpdateSequenceNum);

    fillResourceValue<int, qint16>(
        record, QStringLiteral("width"), resource, &Resource::setWidth);

    fillResourceValue<int, qint16>(
        record, QStringLiteral("height"), resource, &Resource::setHeight);

    fillResourceValue<int, qint32>(
        record, QStringLiteral("dataSize"), resource,
        [](Resource & resource, const qint32 dataSize)
        {
            if (!resource.data()) {
                resource.setData(qevercloud::Data{});
            }
            resource.mutableData()->setSize(dataSize);
        });

    fillResourceValue<QByteArray, QByteArray>(
        record, QStringLiteral("dataHash"), resource,
        [](Resource & resource, QByteArray dataHash)
        {
            if (!resource.data()) {
                resource.setData(qevercloud::Data{});
            }
            resource.mutableData()->setBodyHash(std::move(dataHash));
        });

    fillResourceValue<int, qint32>(
        record, QStringLiteral("recognitionDataSize"), resource,
        [](Resource & resource, const qint32 dataSize)
        {
            if (!resource.recognition()) {
                resource.setRecognition(qevercloud::Data{});
            }
            resource.mutableRecognition()->setSize(dataSize);
        });

    fillResourceValue<QByteArray, QByteArray>(
        record, QStringLiteral("recognitionDataHash"), resource,
        [](Resource & resource, QByteArray dataHash)
        {
            if (!resource.recognition()) {
                resource.setRecognition(qevercloud::Data{});
            }
            resource.mutableRecognition()->setBodyHash(std::move(dataHash));
        });

    fillResourceValue<QByteArray, QByteArray>(
        record, QStringLiteral("recognitionDataBody"), resource,
        [](Resource & resource, QByteArray dataBody)
        {
            if (!resource.recognition()) {
                resource.setRecognition(qevercloud::Data{});
            }
            resource.mutableRecognition()->setBody(std::move(dataBody));
        });

    fillResourceValue<int, qint32>(
        record, QStringLiteral("alternateDataSize"), resource,
        [](Resource & resource, const qint32 dataSize)
        {
            if (!resource.alternateData()) {
                resource.setAlternateData(qevercloud::Data{});
            }
            resource.mutableAlternateData()->setSize(dataSize);
        });

    fillResourceValue<QByteArray, QByteArray>(
        record, QStringLiteral("alternateDataHash"), resource,
        [](Resource & resource, QByteArray dataHash)
        {
            if (!resource.alternateData()) {
                resource.setAlternateData(qevercloud::Data{});
            }
            resource.mutableAlternateData()->setBodyHash(std::move(dataHash));
        });

    const int recordIndex =
        record.indexOf(QStringLiteral("resourceIndexInNote"));
    if (recordIndex >= 0) {
        const QVariant value = record.value(recordIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int index = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(QStringLiteral(
                    "cannot convert resource's index in note to int"));
                QNERROR("local_storage::sql::utils", errorDescription);
                return false;
            }
            indexInNote = index;
        }
    }

    using qevercloud::ResourceAttributes;

    const auto setResourceAttributeFunction = [&](auto setter) {
        return [&, setter = std::move(setter)](
                   std::optional<qevercloud::ResourceAttributes> & attributes,
                   auto value) {
            if (!attributes) {
                attributes = qevercloud::ResourceAttributes{};
            }

            setter(*attributes, std::move(value));
        };
    };

    fillResourceAttributeValue<QString, QString>(
        record, QStringLiteral("resourceSourceURL"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes, QString sourceUrl) {
                attributes.setSourceURL(std::move(sourceUrl));
            }));

    fillResourceAttributeValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("timestamp"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               qevercloud::Timestamp timestamp) {
                attributes.setTimestamp(timestamp);
            }));

    fillResourceAttributeValue<double, double>(
        record, QStringLiteral("resourceLatitude"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               double latitude) {
                attributes.setLatitude(latitude);
            }));

    fillResourceAttributeValue<double, double>(
        record, QStringLiteral("resourceLongitude"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               double longitude) {
                attributes.setLongitude(longitude);
            }));

    fillResourceAttributeValue<double, double>(
        record, QStringLiteral("resourceAltitude"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               double altitude) {
                attributes.setAltitude(altitude);
            }));

    fillResourceAttributeValue<QString, QString>(
        record, QStringLiteral("cameraMake"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               QString cameraMake) {
                attributes.setCameraMake(std::move(cameraMake));
            }));

    fillResourceAttributeValue<QString, QString>(
        record, QStringLiteral("cameraModel"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               QString cameraModel) {
                attributes.setCameraModel(std::move(cameraModel));
            }));

    fillResourceAttributeValue<int, bool>(
        record, QStringLiteral("clientWillIndex"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               bool clientWillIndex) {
                attributes.setClientWillIndex(clientWillIndex);
            }));

    fillResourceAttributeValue<QString, QString>(
        record, QStringLiteral("fileName"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               QString fileName) {
                attributes.setFileName(std::move(fileName));
            }));

    fillResourceAttributeValue<int, bool>(
        record, QStringLiteral("attachment"), resource,
        setResourceAttributeFunction(
            [](qevercloud::ResourceAttributes & attributes,
               bool attachment) {
                attributes.setAttachment(attachment);
            }));

    return true;
}

bool fillSavedSearchFromSqlRecord(
    const QSqlRecord & record, qevercloud::SavedSearch & savedSearch,
    ErrorString & errorDescription)
{
    using qevercloud::SavedSearch;

    if (!fillSavedSearchValue<int, bool>(
            record, QStringLiteral("isDirty"), savedSearch,
            &SavedSearch::setLocallyModified, &errorDescription))
    {
        return false;
    }

    if (!fillSavedSearchValue<int, bool>(
            record, QStringLiteral("isLocal"), savedSearch,
            &SavedSearch::setLocalOnly, &errorDescription))
    {
        return false;
    }

    if (!fillSavedSearchValue<int, bool>(
            record, QStringLiteral("isFavorited"), savedSearch,
            &SavedSearch::setLocallyFavorited, &errorDescription))
    {
        return false;
    }

    if (!fillSavedSearchValue<QString, QString>(
            record, QStringLiteral("localUid"), savedSearch,
            &SavedSearch::setLocalId, &errorDescription))
    {
        return false;
    }

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(SavedSearch &, std::optional<QString>)> setter) {
            fillSavedSearchValue<QString, std::optional<QString>>(
                record, column, savedSearch, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("guid"), &SavedSearch::setGuid);
    fillOptStringValue(QStringLiteral("name"), &SavedSearch::setName);
    fillOptStringValue(QStringLiteral("query"), &SavedSearch::setQuery);

    fillSavedSearchValue<int, qevercloud::QueryFormat>(
        record, QStringLiteral("format"), savedSearch, &SavedSearch::setFormat);

    fillSavedSearchValue<qint32, qint32>(
        record, QStringLiteral("updateSequenceNumber"), savedSearch,
        &SavedSearch::setUpdateSequenceNum);

    const auto setSavedSearchScopeValue =
        [](SavedSearch & savedSearch, auto setter)
        {
            if (!savedSearch.scope()) {
                savedSearch.setScope(qevercloud::SavedSearchScope{});
            }
            setter(*savedSearch.mutableScope());
        };

    fillSavedSearchValue<int, bool>(
        record, QStringLiteral("includeAccount"), savedSearch,
        [&](SavedSearch & savedSearch, bool value)
        {
            setSavedSearchScopeValue(
                savedSearch,
                [value](qevercloud::SavedSearchScope & scope)
                {
                    scope.setIncludeAccount(value);
                });
        });

    fillSavedSearchValue<int, bool>(
        record, QStringLiteral("includePersonalLinkedNotebooks"), savedSearch,
        [&](SavedSearch & savedSearch, bool value)
        {
            setSavedSearchScopeValue(
                savedSearch,
                [value](qevercloud::SavedSearchScope & scope)
                {
                    scope.setIncludePersonalLinkedNotebooks(value);
                });
        });

    fillSavedSearchValue<int, bool>(
        record, QStringLiteral("includeBusinessLinkedNotebooks"), savedSearch,
        [&](SavedSearch & savedSearch, bool value)
        {
            setSavedSearchScopeValue(
                savedSearch,
                [value](qevercloud::SavedSearchScope & scope)
                {
                    scope.setIncludeBusinessLinkedNotebooks(value);
                });
        });

    return true;
}

bool fillNoteFromSqlRecord(
    const QSqlRecord & record, qevercloud::Note & note,
    ErrorString & errorDescription)
{
    using qevercloud::Note;

    if (!fillNoteValue<int, bool>(
            record, QStringLiteral("isDirty"), note,
            &Note::setLocallyModified, &errorDescription))
    {
        return false;
    }

    if (!fillNoteValue<int, bool>(
            record, QStringLiteral("isLocal"), note,
            &Note::setLocalOnly, &errorDescription))
    {
        return false;
    }

    if (!fillNoteValue<int, bool>(
            record, QStringLiteral("isFavorited"), note,
            &Note::setLocallyFavorited, &errorDescription))
    {
        return false;
    }

    if (!fillNoteValue<QString, QString>(
            record, QStringLiteral("localUid"), note,
            &Note::setLocalId, &errorDescription))
    {
        return false;
    }

    fillNoteValue<QString, QString>(
        record, QStringLiteral("notebookLocalUid"), note,
        &Note::setNotebookLocalId);

    const auto fillOptStringValue =
        [&](const QString & column,
            std::function<void(Note &, std::optional<QString>)> setter) {
            fillNoteValue<QString, std::optional<QString>>(
                record, column, note, std::move(setter));
        };

    fillOptStringValue(QStringLiteral("guid"), &Note::setGuid);
    fillOptStringValue(QStringLiteral("notebookGuid"), &Note::setNotebookGuid);
    fillOptStringValue(QStringLiteral("title"), &Note::setTitle);
    fillOptStringValue(QStringLiteral("content"), &Note::setContent);

    fillNoteValue<qint32, qint32>(
        record, QStringLiteral("updateSequenceNumber"), note,
        &Note::setUpdateSequenceNum);

    fillNoteValue<qint32, qint32>(
        record, QStringLiteral("contentLength"), note, &Note::setContentLength);

    fillNoteValue<QByteArray, QByteArray>(
        record, QStringLiteral("contentHash"), note, &Note::setContentHash);

    fillNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("creationTimestamp"), note, &Note::setCreated);

    fillNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("modificationTimestamp"), note,
        &Note::setUpdated);

    fillNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("deletionTimestamp"), note, &Note::setDeleted);

    fillNoteValue<int, bool>(
        record, QStringLiteral("isActive"), note, &Note::setActive);

    const int indexOfThumbnail = record.indexOf(QStringLiteral("thumbnail"));
    if (indexOfThumbnail >= 0) {
        const QVariant thumbnailValue = record.value(indexOfThumbnail);
        if (!thumbnailValue.isNull()) {
            note.setThumbnailData(thumbnailValue.toByteArray());
        }
    }

    const int hasAttributesIndex = record.indexOf(QStringLiteral("hasAttributes"));
    if (hasAttributesIndex >= 0) {
        const QVariant hasAttributesValue = record.value(hasAttributesIndex);
        if (!hasAttributesValue.isNull()) {
            const bool hasAttributes =
                static_cast<bool>(qvariant_cast<int>(hasAttributesValue));
            if (hasAttributes) {
                if (!note.attributes()) {
                    note.setAttributes(qevercloud::NoteAttributes{});
                }

                auto & attributes = *note.mutableAttributes();

                fillNoteAttributesFromSqlRecord(record, attributes);

                fillNoteAttributesApplicationDataKeysOnlyFromSqlRecord(
                    record, attributes);

                fillNoteAttributesApplicationDataFullMapFromSqlRecord(
                    record, attributes);

                fillNoteAttributesClassificationsFromSqlRecord(record, attributes);
            }
        }
    }

    const auto setNoteRestrictionValue =
        [](Note & note, auto setter)
        {
            if (!note.restrictions()) {
                note.setRestrictions(qevercloud::NoteRestrictions{});
            }
            setter(*note.mutableRestrictions());
        };

    fillNoteValue<int, bool>(
        record, QStringLiteral("noUpdateNoteTitle"), note,
        [&](Note & note, bool value)
        {
            setNoteRestrictionValue(
                note,
                [value](qevercloud::NoteRestrictions & restrictions)
                {
                    restrictions.setNoUpdateTitle(value);
                });
        });

    fillNoteValue<int, bool>(
        record, QStringLiteral("noUpdateNoteContent"), note,
        [&](Note & note, bool value)
        {
            setNoteRestrictionValue(
                note,
                [value](qevercloud::NoteRestrictions & restrictions)
                {
                    restrictions.setNoUpdateContent(value);
                });
        });

    fillNoteValue<int, bool>(
        record, QStringLiteral("noEmailNote"), note,
        [&](Note & note, bool value)
        {
            setNoteRestrictionValue(
                note,
                [value](qevercloud::NoteRestrictions & restrictions)
                {
                    restrictions.setNoEmail(value);
                });
        });

    fillNoteValue<int, bool>(
        record, QStringLiteral("noShareNote"), note,
        [&](Note & note, bool value)
        {
            setNoteRestrictionValue(
                note,
                [value](qevercloud::NoteRestrictions & restrictions)
                {
                    restrictions.setNoShare(value);
                });
        });

    fillNoteValue<int, bool>(
        record, QStringLiteral("noShareNotePublicly"), note,
        [&](Note & note, bool value)
        {
            setNoteRestrictionValue(
                note,
                [value](qevercloud::NoteRestrictions & restrictions)
                {
                    restrictions.setNoSharePublicly(value);
                });
        });

    const auto setNoteLimitValue =
        [](Note & note, auto setter)
        {
            if (!note.limits()) {
                note.setLimits(qevercloud::NoteLimits{});
            }
            setter(*note.mutableLimits());
        };

    fillNoteValue<qint32, qint32>(
        record, QStringLiteral("noteResourceCountMax"), note,
        [&](Note & note, qint32 value)
        {
            setNoteLimitValue(
                note,
                [value](qevercloud::NoteLimits & limits)
                {
                    limits.setNoteResourceCountMax(value);
                });
        });

    fillNoteValue<qint64, qint64>(
        record, QStringLiteral("uploadLimit"), note,
        [&](Note & note, qint64 value)
        {
            setNoteLimitValue(
                note,
                [value](qevercloud::NoteLimits & limits)
                {
                    limits.setUploadLimit(value);
                });
        });

    fillNoteValue<qint64, qint64>(
        record, QStringLiteral("resourceSizeMax"), note,
        [&](Note & note, qint64 value)
        {
            setNoteLimitValue(
                note,
                [value](qevercloud::NoteLimits & limits)
                {
                    limits.setResourceSizeMax(value);
                });
        });

    fillNoteValue<qint64, qint64>(
        record, QStringLiteral("noteSizeMax"), note,
        [&](Note & note, qint64 value)
        {
            setNoteLimitValue(
                note,
                [value](qevercloud::NoteLimits & limits)
                {
                    limits.setNoteSizeMax(value);
                });
        });

    fillNoteValue<qint64, qint64>(
        record, QStringLiteral("uploaded"), note,
        [&](Note & note, qint64 value)
        {
            setNoteLimitValue(
                note,
                [value](qevercloud::NoteLimits & limits)
                {
                    limits.setUploaded(value);
                });
        });

    return true;
}

bool fillSharedNoteFromSqlRecord(
    const QSqlRecord & record, qevercloud::SharedNote & sharedNote,
    int & indexInNote, ErrorString & errorDescription)
{
    using qevercloud::SharedNote;

    fillSharedNoteValue<QString, QString>(
        record, QStringLiteral("sharedNoteNoteGuid"), sharedNote,
        &SharedNote::setNoteGuid);

    fillSharedNoteValue<qint32, qint32>(
        record, QStringLiteral("sharedNoteSharerUserId"), sharedNote,
        &SharedNote::setSharerUserID);

    fillSharedNoteValue<qint8, qevercloud::SharedNotePrivilegeLevel>(
        record, QStringLiteral("sharedNotePrivilegeLevel"), sharedNote,
        &SharedNote::setPrivilege);

    fillSharedNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNoteCreationTimestamp"), sharedNote,
        &SharedNote::setServiceCreated);

    fillSharedNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNoteModificationTimestamp"), sharedNote,
        &SharedNote::setServiceUpdated);

    fillSharedNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNoteAssignmentTimestamp"), sharedNote,
        &SharedNote::setServiceAssigned);

    const auto setSharedNoteRecipientIdentityValue =
        [](SharedNote & sharedNote, auto setter)
        {
            if (!sharedNote.recipientIdentity()) {
                sharedNote.setRecipientIdentity(qevercloud::Identity{});
            }
            setter(*sharedNote.mutableRecipientIdentity());
        };

    fillSharedNoteValue<qint64, qevercloud::IdentityID>(
        record, QStringLiteral("sharedNoteRecipientIdentityId"), sharedNote,
        [&](SharedNote & sharedNote, qevercloud::IdentityID identityId)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [identityId](qevercloud::Identity & identity)
                {
                    identity.setId(identityId);
                });
        });

    fillSharedNoteValue<qint64, qevercloud::UserID>(
        record, QStringLiteral("sharedNoteRecipientUserId"), sharedNote,
        [&](SharedNote & sharedNote, qevercloud::UserID userId)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [userId](qevercloud::Identity & identity)
                {
                    identity.setUserId(userId);
                });
        });

    fillSharedNoteValue<int, bool>(
        record, QStringLiteral("sharedNoteRecipientDeactivated"), sharedNote,
        [&](SharedNote & sharedNote, bool deactivated)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [deactivated](qevercloud::Identity & identity)
                {
                    identity.setDeactivated(deactivated);
                });
        });

    fillSharedNoteValue<int, bool>(
        record, QStringLiteral("sharedNoteRecipientSameBusiness"), sharedNote,
        [&](SharedNote & sharedNote, bool sameBusiness)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [sameBusiness](qevercloud::Identity & identity)
                {
                    identity.setSameBusiness(sameBusiness);
                });
        });

    fillSharedNoteValue<int, bool>(
        record, QStringLiteral("sharedNoteRecipientBlocked"), sharedNote,
        [&](SharedNote & sharedNote, bool blocked)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [blocked](qevercloud::Identity & identity)
                {
                    identity.setBlocked(blocked);
                });
        });

    fillSharedNoteValue<int, bool>(
        record, QStringLiteral("sharedNoteRecipientUserConnected"), sharedNote,
        [&](SharedNote & sharedNote, bool connected)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [connected](qevercloud::Identity & identity)
                {
                    identity.setUserConnected(connected);
                });
        });

    fillSharedNoteValue<qint64, qevercloud::MessageEventID>(
        record, QStringLiteral("sharedNoteRecipientEventId"), sharedNote,
        [&](SharedNote & sharedNote, qevercloud::MessageEventID id)
        {
            setSharedNoteRecipientIdentityValue(
                sharedNote,
                [id](qevercloud::Identity & identity)
                {
                    identity.setEventId(id);
                });
        });

    const auto setSharedNoteRecipientIdentityContactValue =
        [](SharedNote & sharedNote, auto setter)
        {
            if (!sharedNote.recipientIdentity()) {
                sharedNote.setRecipientIdentity(qevercloud::Identity{});
            }
            if (!sharedNote.recipientIdentity()->contact()) {
                sharedNote.mutableRecipientIdentity()->setContact(
                    qevercloud::Contact{});
            }
            setter(*sharedNote.mutableRecipientIdentity()->mutableContact());
        };

    fillSharedNoteValue<QString, QString>(
        record, QStringLiteral("sharedNoteRecipientContactName"), sharedNote,
        [&](SharedNote & sharedNote, QString name)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setName(std::move(name));
                });
        });

    fillSharedNoteValue<QString, QString>(
        record, QStringLiteral("sharedNoteRecipientContactId"), sharedNote,
        [&](SharedNote & sharedNote, QString id)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setId(std::move(id));
                });
        });

    fillSharedNoteValue<qint32, qevercloud::ContactType>(
        record, QStringLiteral("sharedNoteRecipientContactType"), sharedNote,
        [&](SharedNote & sharedNote, qevercloud::ContactType contactType)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setType(contactType);
                });
        });

    fillSharedNoteValue<QString, QString>(
        record, QStringLiteral("sharedNoteRecipientContactPhotoUrl"),
        sharedNote,
        [&](SharedNote & sharedNote, QString photoUrl)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setPhotoUrl(std::move(photoUrl));
                });
        });

    fillSharedNoteValue<qint64, qevercloud::Timestamp>(
        record, QStringLiteral("sharedNoteRecipientContactPhotoLastUpdated"),
        sharedNote,
        [&](SharedNote & sharedNote, qevercloud::Timestamp timestamp)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [timestamp](qevercloud::Contact & contact)
                {
                    contact.setPhotoLastUpdated(timestamp);
                });
        });

    fillSharedNoteValue<QByteArray, QByteArray>(
        record, QStringLiteral("sharedNoteRecipientContactMessagingPermit"),
        sharedNote,
        [&](SharedNote & sharedNote, QByteArray permit)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setMessagingPermit(std::move(permit));
                });
        });

    fillSharedNoteValue<qint64, qevercloud::Timestamp>(
        record,
        QStringLiteral("sharedNoteRecipientContactMessagingPermitExpires"),
        sharedNote,
        [&](SharedNote & sharedNote, qevercloud::Timestamp timestamp)
        {
            setSharedNoteRecipientIdentityContactValue(
                sharedNote,
                [&](qevercloud::Contact & contact)
                {
                    contact.setMessagingPermitExpires(timestamp);
                });
        });

    const int recordIndex = record.indexOf(QStringLiteral("indexInNote"));
    if (recordIndex >= 0) {
        const QVariant value = record.value(recordIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int index = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(QStringLiteral(
                    "can't convert shared note's index in note to int"));
                QNERROR("local_storage::sql::utils", errorDescription);
                return false;
            }
            indexInNote = index;
        }
    }

    return true;
}

template <>
bool fillObjectFromSqlRecord<qevercloud::Notebook>(
    const QSqlRecord & record, qevercloud::Notebook & object,
    ErrorString & errorDescription)
{
    return fillNotebookFromSqlRecord(record, object, errorDescription);
}

template <>
bool fillObjectFromSqlRecord<qevercloud::SavedSearch>(
    const QSqlRecord & record, qevercloud::SavedSearch & object,
    ErrorString & errorDescription)
{
    return fillSavedSearchFromSqlRecord(record, object, errorDescription);
}

template <>
bool fillObjectFromSqlRecord<qevercloud::Tag>(
    const QSqlRecord & record, qevercloud::Tag & object,
    ErrorString & errorDescription)
{
    return fillTagFromSqlRecord(record, object, errorDescription);
}

template <>
bool fillObjectFromSqlRecord<qevercloud::LinkedNotebook>(
    const QSqlRecord & record, qevercloud::LinkedNotebook & object,
    ErrorString & errorDescription)
{
    return fillLinkedNotebookFromSqlRecord(record, object, errorDescription);
}

template <>
bool fillObjectFromSqlRecord<qevercloud::Resource>(
    const QSqlRecord & record, qevercloud::Resource & object,
    ErrorString & errorDescription)
{
    int indexInNote = -1;
    return fillResourceFromSqlRecord(
        record, object, indexInNote, errorDescription);
}

template <>
bool fillObjectFromSqlRecord<qevercloud::Note>(
    const QSqlRecord & record, qevercloud::Note & object,
    ErrorString & errorDescription)
{
    return fillNoteFromSqlRecord(record, object, errorDescription);
}

template <>
bool fillObjectsFromSqlQuery<qevercloud::Notebook>(
    QSqlQuery & query, QSqlDatabase & database,
    QList<qevercloud::Notebook> & objects, ErrorString & errorDescription)
{
    QMap<QString, int> indexForLocalId;

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int localIdIndex = rec.indexOf(QStringLiteral("localUid"));
        if (localIdIndex < 0) {
            errorDescription.setBase(QStringLiteral(
                "no localUid field in SQL record for notebook"));
            QNWARNING("local_storage::sql::utils", errorDescription);
            return false;
        }

        const QString localId = rec.value(localIdIndex).toString();
        if (localId.isEmpty()) {
            errorDescription.setBase(QStringLiteral(
                "found empty localUid field in SQL record for Notebook"));
            QNWARNING("local_storage::sql::utils", errorDescription);
            return false;
        }

        const auto it = indexForLocalId.find(localId);
        const bool notFound = (it == indexForLocalId.end());
        if (notFound) {
            indexForLocalId[localId] = objects.size();
            objects << qevercloud::Notebook{};
        }

        auto & notebook = (notFound ? objects.back() : objects[it.value()]);

        if (!fillNotebookFromSqlRecord(rec, notebook, errorDescription)) {
            return false;
        }

        if (notebook.guid())
        {
            ErrorString error;
            auto sharedNotebooks = listSharedNotebooks(
                *notebook.guid(), database, error);
            if (!error.isEmpty()) {
                errorDescription.base() = error.base();
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage::sql::utils", errorDescription);
                return false;
            }

            if (!sharedNotebooks.isEmpty()) {
                notebook.setSharedNotebooks(std::move(sharedNotebooks));
            }
        }
    }

    return true;
}

} // namespace quentier::local_storage::sql::utils
