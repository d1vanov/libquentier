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

#include <qevercloud/types/User.h>

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <QGlobalStatic>
#include <QSqlRecord>

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
    return fillValue<qevercloud::User, VariantType, LocalType>(
        record, column, notebook, std::move(setter),
        *gMissingNotebookFieldErrorMessage, errorDescription);
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

} // namespace quentier::local_storage::sql::utils
