/*
 * Copyright 2021 Dmitry Ivanov
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
    (QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "User field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingNotebookFieldErrorMessage,
    (QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Notebook field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingTagFieldErrorMessage,
    (QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Tag field missing in the record received from the local storage "
        "database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingLinkedNotebookFieldErrorMessage,
    (QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "LinkedNotebook field missing in the record received from the local "
        "storage database")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString,
    gMissingResourceFieldErrorMessage,
    (QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Resource field missing in the record received from the local "
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
                std::is_convertible_v<VariantType, LocalType>) {
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
                    "cannot convert resource's index in note to int"));
                QNERROR("local_storage::sql::utils", errorDescription);
                return false;
            }
            indexInNote = index;
        }
    }

    // TODO: implement further: fill resource attributes including application
    // data
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
bool fillObjectsFromSqlQuery<qevercloud::Notebook>(
    QSqlQuery & query, QSqlDatabase & database,
    QList<qevercloud::Notebook> & objects, ErrorString & errorDescription)
{
    QMap<QString, int> indexForLocalId;

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int localIdIndex = rec.indexOf(QStringLiteral("localUid"));
        if (localIdIndex < 0) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "no localUid field in SQL record for notebook"));
            QNWARNING("local_storage::sql::utils", errorDescription);
            return false;
        }

        const QString localId = rec.value(localIdIndex).toString();
        if (localId.isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
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
