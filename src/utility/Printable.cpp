/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include <quentier/utility/Printable.h>
#include <quentier/utility/Utility.h>

namespace quentier {

const QString Printable::toString() const
{
    QString str;
    QTextStream strm(&str, QIODevice::WriteOnly);
    strm << *this;
    return str;
}

Printable::Printable()
{}

Printable::Printable(const Printable &)
{}

Printable::~Printable()
{}

QDebug & operator <<(QDebug & debug, const Printable & printable)
{
    debug << printable.toString();
    return debug;
}

QTextStream & operator <<(QTextStream & strm,
                          const Printable & printable)
{
    return printable.print(strm);
}

} // namespace quentier

#define PRINT_FIELD(obj, field, ...)                                           \
    strm << indent <<  #field " = "                                            \
         << (obj.field.isSet()                                                 \
             ? __VA_ARGS__(obj.field.ref())                                    \
             : QStringLiteral("<empty>")) << "; \n"                            \
// PRINT_FIELD

#define PRINT_LIST_FIELD(obj, field, ...)                                      \
    strm << indent <<  #field;                                                 \
    if (obj.field.isSet())                                                     \
    {                                                                          \
        strm << ": { \n";                                                      \
        const auto & field##List = obj.field.ref();                            \
        const int num##field = field##List.size();                             \
        for(int i = 0; i < num##field; ++i) {                                  \
            strm << indent << indent << "[" << i                               \
                 << "]: "                                                      \
                 << __VA_ARGS__(field##List[i]) << ";\n";                      \
        }                                                                      \
        strm << indent << "};\n";                                              \
    }                                                                          \
    else                                                                       \
    {                                                                          \
        strm << "<empty>;\n";                                                  \
    }                                                                          \
// PRINT_LIST_FIELD

#define PRINT_APPLICATION_DATA(obj)                                            \
    if (obj.applicationData.isSet())                                           \
    {                                                                          \
        const qevercloud::LazyMap & applicationData = obj.applicationData;     \
        if (applicationData.keysOnly.isSet())                                  \
        {                                                                      \
            const QSet<QString> & keysOnly = applicationData.keysOnly;         \
            strm << indent << "applicationData: keys only: \n";                \
            for(auto it = keysOnly.begin(),                                    \
                end = keysOnly.end(); it != end; ++it)                         \
            {                                                                  \
                strm << *it << "; ";                                           \
            }                                                                  \
            strm << "\n";                                                      \
        }                                                                      \
                                                                               \
        if (applicationData.fullMap.isSet())                                   \
        {                                                                      \
            const QMap<QString, QString> & fullMap = applicationData.fullMap;  \
            strm << indent << "applicationData: full map: \n";                 \
            for(auto it = fullMap.begin(),                                     \
                end = fullMap.end(); it != end; ++it)                          \
            {                                                                  \
                strm << "[" << it.key() << "] = "                              \
                     << it.value() << "; ";                                    \
            }                                                                  \
            strm << "\n";                                                      \
        }                                                                      \
    }                                                                          \
// PRINT_APPLICATION_DATA

QString byteArrayToHex(const QByteArray & bytes)
{
    return QString::fromLocal8Bit(bytes.toHex());
}

QString contactTypeToString(const qevercloud::ContactType::type & type)
{
    switch(type)
    {
    case qevercloud::ContactType::EVERNOTE:
        return QStringLiteral("EVERNOTE");
    case qevercloud::ContactType::SMS:
        return QStringLiteral("SMS");
    case qevercloud::ContactType::FACEBOOK:
        return QStringLiteral("FACEBOOK");
    case qevercloud::ContactType::EMAIL:
        return QStringLiteral("EMAIL");
    case qevercloud::ContactType::TWITTER:
        return QStringLiteral("TWITTER");
    case qevercloud::ContactType::LINKEDIN:
        return QStringLiteral("LINKEDIN");
    default:
        return QStringLiteral("Unknown");
    }
}

QTextStream & operator <<(QTextStream & strm, const qevercloud::Contact & contact)
{
    strm << "qevercloud::Contact: {\n";
    const char * indent = "  ";

    PRINT_FIELD(contact, name);
    PRINT_FIELD(contact, id);
    PRINT_FIELD(contact, type, contactTypeToString);
    PRINT_FIELD(contact, photoUrl);
    PRINT_FIELD(contact, photoLastUpdated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(contact, messagingPermit, QString::fromUtf8);
    PRINT_FIELD(contact, messagingPermitExpires,
                quentier::printableDateTimeFromTimestamp);

    strm << "}; \n";
    return strm;
}

QString boolToString(const bool value)
{
    return (value ? QStringLiteral("true") : QStringLiteral("false"));
}

QTextStream & operator <<(QTextStream & strm, const qevercloud::Identity & identity)
{
    strm << "qevercloud::Identity: {\n";
    const char * indent = "  ";

    strm << indent << "id = " << QString::number(identity.id) << ";\n";

    PRINT_FIELD(identity, contact, ToString);
    PRINT_FIELD(identity, userId, QString::number);
    PRINT_FIELD(identity, deactivated, boolToString);
    PRINT_FIELD(identity, sameBusiness, boolToString);
    PRINT_FIELD(identity, blocked, boolToString);
    PRINT_FIELD(identity, userConnected, boolToString);
    PRINT_FIELD(identity, eventId, QString::number);

    strm << "}; \n";
    return strm;
}

QTextStream & operator <<(QTextStream & strm, const qevercloud::BusinessUserInfo & info)
{
    strm << "qevercloud::BusinessUserInfo: {\n";
    const char * indent = "  ";

    PRINT_FIELD(info, businessId, QString::number);
    PRINT_FIELD(info, businessName);
    PRINT_FIELD(info, role, ToString);
    PRINT_FIELD(info, email);
    PRINT_FIELD(info, updated, quentier::printableDateTimeFromTimestamp);

    strm << "}; \n";
    return strm;
}

QString premiumOrderStatusToString(const qevercloud::PremiumOrderStatus::type & status)
{
    switch(status)
    {
    case qevercloud::PremiumOrderStatus::NONE:
        return QStringLiteral("NONE");
    case qevercloud::PremiumOrderStatus::PENDING:
        return QStringLiteral("PENDING");
    case qevercloud::PremiumOrderStatus::ACTIVE:
        return QStringLiteral("ACTIVE");
    case qevercloud::PremiumOrderStatus::FAILED:
        return QStringLiteral("FAILED");
    case qevercloud::PremiumOrderStatus::CANCELLATION_PENDING:
        return QStringLiteral("CANCELLATION_PENDING");
    case qevercloud::PremiumOrderStatus::CANCELED:
        return QStringLiteral("CANCELED");
    default:
        return QStringLiteral("Unknown");
    }
}

QString businessUserRoleToString(const qevercloud::BusinessUserRole::type & role)
{
    switch(role)
    {
    case qevercloud::BusinessUserRole::ADMIN:
        return QStringLiteral("ADMIN");
    case qevercloud::BusinessUserRole::NORMAL:
        return QStringLiteral("NORMAL");
    default:
        return QStringLiteral("Unknown");
    }
}

QTextStream & operator <<(QTextStream & strm, const qevercloud::Accounting & accounting)
{
    strm << "qevercloud::Accounting: { \n";
    const char * indent = "  ";

    PRINT_FIELD(accounting, uploadLimitEnd,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, uploadLimitNextMonth, QString::number);
    PRINT_FIELD(accounting, premiumServiceStatus, premiumOrderStatusToString);
    PRINT_FIELD(accounting, premiumOrderNumber);
    PRINT_FIELD(accounting, premiumCommerceService);
    PRINT_FIELD(accounting, premiumServiceStart,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, premiumServiceSKU);
    PRINT_FIELD(accounting, lastSuccessfulCharge,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, lastFailedCharge,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, lastFailedChargeReason);
    PRINT_FIELD(accounting, nextPaymentDue,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, premiumLockUntil,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, updated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, premiumSubscriptionNumber);
    PRINT_FIELD(accounting, lastRequestedCharge,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, currency);
    PRINT_FIELD(accounting, unitPrice,
                QString::number);
    PRINT_FIELD(accounting, businessId,
                QString::number);
    PRINT_FIELD(accounting, businessRole, businessUserRoleToString);
    PRINT_FIELD(accounting, unitDiscount,
                QString::number);
    PRINT_FIELD(accounting, nextChargeDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(accounting, availablePoints,
                QString::number);

    strm << "}; \n";
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::AccountLimits & limits)
{
    strm << "qevercloud::AccountLimits: {\n";
    const char * indent = "  ";

    PRINT_FIELD(limits, userMailLimitDaily, QString::number);
    PRINT_FIELD(limits, noteSizeMax, QString::number);
    PRINT_FIELD(limits, resourceSizeMax, QString::number);
    PRINT_FIELD(limits, userLinkedNotebookMax, QString::number);
    PRINT_FIELD(limits, uploadLimit, QString::number);
    PRINT_FIELD(limits, userNoteCountMax, QString::number);
    PRINT_FIELD(limits, userNotebookCountMax, QString::number);
    PRINT_FIELD(limits, userTagCountMax, QString::number);
    PRINT_FIELD(limits, noteTagCountMax, QString::number);
    PRINT_FIELD(limits, userSavedSearchesMax, QString::number);
    PRINT_FIELD(limits, noteResourceCountMax, QString::number);

    return strm;
}

QString reminderEmailConfigToString(
    const qevercloud::ReminderEmailConfig::type & type)
{
    switch(type)
    {
    case qevercloud::ReminderEmailConfig::DO_NOT_SEND:
        return QStringLiteral("DO NOT SEND");
    case qevercloud::ReminderEmailConfig::SEND_DAILY_EMAIL:
        return QStringLiteral("SEND DAILY EMAIL");
    default:
        return QStringLiteral("Unknown");
    }
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::UserAttributes & attributes)
{
    strm << "qevercloud::UserAttributes: {\n";
    const char * indent = "  ";

    PRINT_FIELD(attributes, defaultLocationName);
    PRINT_FIELD(attributes, defaultLatitude, QString::number);
    PRINT_FIELD(attributes, defaultLongitude, QString::number);
    PRINT_FIELD(attributes, preactivation, boolToString);
    PRINT_LIST_FIELD(attributes, viewedPromotions)
    PRINT_FIELD(attributes, incomingEmailAddress);
    PRINT_LIST_FIELD(attributes, recentMailedAddresses)
    PRINT_FIELD(attributes, comments);
    PRINT_FIELD(attributes, dateAgreedToTermsOfService,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, maxReferrals, QString::number);
    PRINT_FIELD(attributes, referralCount, QString::number);
    PRINT_FIELD(attributes, refererCode);
    PRINT_FIELD(attributes, sentEmailDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, dailyEmailLimit, QString::number);
    PRINT_FIELD(attributes, emailOptOutDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, partnerEmailOptInDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, preferredLanguage);
    PRINT_FIELD(attributes, preferredCountry);
    PRINT_FIELD(attributes, clipFullPage, boolToString);
    PRINT_FIELD(attributes, twitterUserName);
    PRINT_FIELD(attributes, twitterId);
    PRINT_FIELD(attributes, groupName);
    PRINT_FIELD(attributes, recognitionLanguage);
    PRINT_FIELD(attributes, referralProof);
    PRINT_FIELD(attributes, educationalDiscount, boolToString);
    PRINT_FIELD(attributes, businessAddress);
    PRINT_FIELD(attributes, hideSponsorBilling, boolToString);
    PRINT_FIELD(attributes, useEmailAutoFiling, boolToString);
    PRINT_FIELD(attributes, reminderEmailConfig, reminderEmailConfigToString);
    PRINT_FIELD(attributes, passwordUpdated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, salesforcePushEnabled, boolToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::NoteAttributes & attributes)
{
    strm << "qevercloud::NoteAttributes: {\n";
    const char * indent = "  ";

    PRINT_FIELD(attributes, subjectDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, latitude, QString::number);
    PRINT_FIELD(attributes, longitude, QString::number);
    PRINT_FIELD(attributes, altitude, QString::number);
    PRINT_FIELD(attributes, author);
    PRINT_FIELD(attributes, source);
    PRINT_FIELD(attributes, sourceURL);
    PRINT_FIELD(attributes, sourceApplication);
    PRINT_FIELD(attributes, shareDate,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, reminderOrder, QString::number);
    PRINT_FIELD(attributes, reminderDoneTime,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, reminderTime,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, placeName);
    PRINT_FIELD(attributes, contentClass);
    PRINT_FIELD(attributes, lastEditedBy);
    PRINT_FIELD(attributes, creatorId, QString::number);
    PRINT_FIELD(attributes, lastEditorId, QString::number);
    PRINT_FIELD(attributes, sharedWithBusiness, boolToString);
    PRINT_FIELD(attributes, conflictSourceNoteGuid);
    PRINT_FIELD(attributes, noteTitleQuality, QString::number);

    PRINT_APPLICATION_DATA(attributes)

    if (attributes.classifications.isSet())
    {
        strm << indent << "classifications: ";
        const QMap<QString, QString> & classifications = attributes.classifications;
        for(auto it = classifications.begin(),
            end = classifications.end(); it != end; ++it)
        {
            strm << "[" << it.key() << "] = "
                 << it.value() << "; ";
        }
        strm << "\n";
    }

    strm << "};\n";
    return strm;
}

QString privilegeLevelToString(const qevercloud::PrivilegeLevel::type & level)
{
    switch (level)
    {
    case qevercloud::PrivilegeLevel::NORMAL:
        return QStringLiteral("NORMAL");
    case qevercloud::PrivilegeLevel::PREMIUM:
        return QStringLiteral("PREMIUM");
    case qevercloud::PrivilegeLevel::VIP:
        return QStringLiteral("VIP");
    case qevercloud::PrivilegeLevel::MANAGER:
        return QStringLiteral("MANAGER");
    case qevercloud::PrivilegeLevel::SUPPORT:
        return QStringLiteral("SUPPORT");
    case qevercloud::PrivilegeLevel::ADMIN:
        return QStringLiteral("ADMIN");
    default:
        return QStringLiteral("Unknown");
    }
}

QString serviceLevelToString(const qevercloud::ServiceLevel::type & level)
{
    switch(level)
    {
    case qevercloud::ServiceLevel::BASIC:
        return QStringLiteral("BASIC");
    case qevercloud::ServiceLevel::PLUS:
        return QStringLiteral("PLUS");
    case qevercloud::ServiceLevel::PREMIUM:
        return QStringLiteral("PREMIUM");
    case qevercloud::ServiceLevel::BUSINESS:
        return QStringLiteral("BUSINESS");
    default:
        return QStringLiteral("Unknown");
    }
}

QString queryFormatToString(const qevercloud::QueryFormat::type & format)
{
    switch (format)
    {
    case qevercloud::QueryFormat::USER:
        return QStringLiteral("USER");
    case qevercloud::QueryFormat::SEXP:
        return QStringLiteral("SEXP");
    default:
        return QStringLiteral("Unknown");
    }
}

QString sharedNotebookPrivilegeLevelToString(
    const qevercloud::SharedNotebookPrivilegeLevel::type & privilege)
{
    switch(privilege)
    {
    case qevercloud::SharedNotebookPrivilegeLevel::READ_NOTEBOOK:
        return QStringLiteral("READ_NOTEBOOK");
    case qevercloud::SharedNotebookPrivilegeLevel::MODIFY_NOTEBOOK_PLUS_ACTIVITY:
        return QStringLiteral("MODIFY_NOTEBOOK_PLUS_ACTIVITY");
    case qevercloud::SharedNotebookPrivilegeLevel::READ_NOTEBOOK_PLUS_ACTIVITY:
        return QStringLiteral("READ_NOTEBOOK_PLUS_ACTIVITY");
    case qevercloud::SharedNotebookPrivilegeLevel::GROUP:
        return QStringLiteral("GROUP");
    case qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS:
        return QStringLiteral("FULL_ACCESS");
    case qevercloud::SharedNotebookPrivilegeLevel::BUSINESS_FULL_ACCESS:
        return QStringLiteral("BUSINESS_FULL_ACCESS");
    default:
        return QStringLiteral("Unknown");
    }
}

QString noteSortOrderToString(const qevercloud::NoteSortOrder::type & order)
{
    switch(order)
    {
    case qevercloud::NoteSortOrder::CREATED:
        return QStringLiteral("CREATED");
    case qevercloud::NoteSortOrder::RELEVANCE:
        return QStringLiteral("RELEVANCE");
    case qevercloud::NoteSortOrder::TITLE:
        return QStringLiteral("TITLE");
    case qevercloud::NoteSortOrder::UPDATED:
        return QStringLiteral("UPDATED");
    case qevercloud::NoteSortOrder::UPDATE_SEQUENCE_NUMBER:
        return QStringLiteral("UPDATE_SEQUENCE_NUMBER");
    default:
        return QStringLiteral("Unknown");
    }
}

QString sharedNotebookInstanceRestrictionsToString(
    const qevercloud::SharedNotebookInstanceRestrictions::type & restrictions)
{
    switch(restrictions)
    {
    case qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED:
        return QStringLiteral("ASSIGNED");
    case qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS:
        return QStringLiteral("NO_SHARED_NOTEBOOKS");
    default:
        return QStringLiteral("Unknown");
    }
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::QueryFormat::type & queryFormat)
{
    strm << queryFormatToString(queryFormat);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::ServiceLevel::type & level)
{
    strm << serviceLevelToString(level);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::PrivilegeLevel::type & level)
{
    strm << privilegeLevelToString(level);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::SharedNotebookPrivilegeLevel::type & level)
{
    strm << sharedNotebookPrivilegeLevelToString(level);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::NoteSortOrder::type & noteSortOrder)
{
    strm << noteSortOrderToString(noteSortOrder);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::NotebookRestrictions & restrictions)
{
    strm << "NotebookRestrictions: {\n";
    const char * indent = "  ";

    PRINT_FIELD(restrictions, noReadNotes, boolToString);
    PRINT_FIELD(restrictions, noCreateNotes, boolToString);
    PRINT_FIELD(restrictions, noUpdateNotes, boolToString);
    PRINT_FIELD(restrictions, noExpungeNotes, boolToString);
    PRINT_FIELD(restrictions, noShareNotes, boolToString);
    PRINT_FIELD(restrictions, noEmailNotes, boolToString);
    PRINT_FIELD(restrictions, noSendMessageToRecipients, boolToString);
    PRINT_FIELD(restrictions, noUpdateNotebook, boolToString);
    PRINT_FIELD(restrictions, noExpungeNotebook, boolToString);
    PRINT_FIELD(restrictions, noSetDefaultNotebook, boolToString);
    PRINT_FIELD(restrictions, noSetNotebookStack, boolToString);
    PRINT_FIELD(restrictions, noPublishToPublic, boolToString);
    PRINT_FIELD(restrictions, noPublishToBusinessLibrary, boolToString);
    PRINT_FIELD(restrictions, noCreateTags, boolToString);
    PRINT_FIELD(restrictions, noUpdateTags, boolToString);
    PRINT_FIELD(restrictions, noExpungeTags, boolToString);
    PRINT_FIELD(restrictions, noSetParentTag, boolToString);
    PRINT_FIELD(restrictions, noCreateSharedNotebooks, boolToString);
    PRINT_FIELD(restrictions, noShareNotesWithBusiness, boolToString);
    PRINT_FIELD(restrictions, noRenameNotebook, boolToString);
    PRINT_FIELD(restrictions, updateWhichSharedNotebookRestrictions,
                sharedNotebookInstanceRestrictionsToString);
    PRINT_FIELD(restrictions, expungeWhichSharedNotebookRestrictions,
                sharedNotebookInstanceRestrictionsToString);

    strm << "};\n";

    return strm;
}

QTextStream & operator<<(
    QTextStream & strm,
    const qevercloud::SharedNotebookInstanceRestrictions::type & restrictions)
{
    strm << sharedNotebookInstanceRestrictionsToString(restrictions);
    return strm;
}

QTextStream & operator <<(QTextStream & strm,
                          const qevercloud::ResourceAttributes & attributes)
{
    strm << "ResourceAttributes: {\n";
    const char * indent = "  ";

    PRINT_FIELD(attributes, sourceURL);
    PRINT_FIELD(attributes, timestamp, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(attributes, latitude, QString::number);
    PRINT_FIELD(attributes, longitude, QString::number);
    PRINT_FIELD(attributes, altitude, QString::number);
    PRINT_FIELD(attributes, cameraMake);
    PRINT_FIELD(attributes, cameraModel);
    PRINT_FIELD(attributes, clientWillIndex, boolToString);
    PRINT_FIELD(attributes, fileName);
    PRINT_FIELD(attributes, attachment, boolToString);
    PRINT_APPLICATION_DATA(attributes)

    strm << "};\n";
    return strm;
}

QTextStream & operator <<(QTextStream & strm, const qevercloud::Resource & resource)
{
    strm << "qevercloud::Resource {\n";
    const char * indent = "  ";

    PRINT_FIELD(resource, guid);
    PRINT_FIELD(resource, updateSequenceNum, QString::number);
    PRINT_FIELD(resource, noteGuid);

    if (resource.data.isSet())
    {
        strm << indent << "Data: {\n";

        PRINT_FIELD(resource.data.ref(), size, QString::number);
        PRINT_FIELD(resource.data.ref(), bodyHash, byteArrayToHex);

        strm << indent << "body: "
             << (resource.data->body.isSet()
                 ? "is set"
                 : "is not set")
             << ";\n";
        if (resource.data->body.isSet() && (resource.data->body->size() < 4096))
        {
            strm << indent << "raw data body content: "
                 << resource.data->body.ref() << ";\n";
        }

        strm << indent << "};\n";
    }

    PRINT_FIELD(resource, mime);
    PRINT_FIELD(resource, width, QString::number);
    PRINT_FIELD(resource, height, QString::number);

    if (resource.recognition.isSet())
    {
        strm << indent << "Recognition data: {\n";

        PRINT_FIELD(resource.recognition.ref(), size, QString::number);
        PRINT_FIELD(resource.recognition.ref(), bodyHash, byteArrayToHex);
        PRINT_FIELD(resource.recognition.ref(), body, QString::fromUtf8);

        if (resource.recognition->body.isSet() &&
            (resource.recognition->body->size() < 4096))
        {
            strm << indent << "raw recognition body content: "
                 << resource.recognition->body.ref() << ";\n";
        }

        strm << indent << "};\n";
    }

    if (resource.alternateData.isSet())
    {
        strm << indent << "Alternate data: {\n";

        PRINT_FIELD(resource.alternateData.ref(), size, QString::number);
        PRINT_FIELD(resource.alternateData.ref(), bodyHash, byteArrayToHex);

        strm << indent << "body: "
             << (resource.alternateData->body.isSet()
                 ? "is set"
                 : "is not set")
             << ";\n";
        if (resource.alternateData->body.isSet() &&
            (resource.alternateData->body->size() < 4096))
        {
            strm << indent << "raw alternate data body content: "
                 << resource.alternateData->body.ref() << ";\n";
        }

        strm << indent << "};\n";
    }

    if (resource.attributes.isSet()) {
        strm << indent << resource.attributes.ref();
    }

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm, const qevercloud::SyncChunk & syncChunk)
{
    strm << "qevercloud::SyncChunk: {\n";
    const char * indent = "  ";

    PRINT_FIELD(syncChunk, chunkHighUSN, QString::number);

    strm << indent << "currentTime = "
         << quentier::printableDateTimeFromTimestamp(syncChunk.currentTime)
         << ";\n";
    strm << indent << "updateCount = " << syncChunk.updateCount << ";\n";

    if (syncChunk.notes.isSet())
    {
        for(auto it = syncChunk.notes->constBegin(),
            end = syncChunk.notes->constEnd(); it != end; ++it)
        {
            strm << indent << "note: guid = "
                 << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.notebooks.isSet())
    {
        for(auto it = syncChunk.notebooks->constBegin(),
            end = syncChunk.notebooks->constEnd(); it != end; ++it)
        {
            strm << indent << "notebook: guid = "
                << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.tags.isSet())
    {
        for(auto it = syncChunk.tags->constBegin(),
            end = syncChunk.tags->constEnd(); it != end; ++it)
        {
            strm << indent << "tag: guid = "
                 << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.searches.isSet())
    {
        for(auto it = syncChunk.searches->constBegin(),
            end = syncChunk.searches->constEnd(); it != end; ++it)
        {
            strm << indent << "saved search: guid = "
                 << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.resources.isSet())
    {
        for(auto it = syncChunk.resources->constBegin(),
            end = syncChunk.resources->constEnd(); it != end; ++it)
        {
            strm << indent << "resource: guid = "
                 << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ", note guid = "
                 << (it->noteGuid.isSet()
                     ? it->noteGuid.ref()
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.linkedNotebooks.isSet())
    {
        for(auto it = syncChunk.linkedNotebooks->constBegin(),
            end = syncChunk.linkedNotebooks->constEnd(); it != end; ++it)
        {
            strm << indent << "linked notebook: guid = "
                 << (it->guid.isSet() ? it->guid.ref() : QStringLiteral("<empty>"))
                 << ", update sequence number = "
                 << (it->updateSequenceNum.isSet()
                     ? QString::number(it->updateSequenceNum.ref())
                     : QStringLiteral("<empty>"))
                 << ";\n";
        }
    }

    if (syncChunk.expungedLinkedNotebooks.isSet())
    {
        for(auto it = syncChunk.expungedLinkedNotebooks->constBegin(),
            end = syncChunk.expungedLinkedNotebooks->constEnd(); it != end; ++it)
        {
            strm << indent << "expunged linked notebook guid = " << *it << ";\n";
        }
    }

    if (syncChunk.expungedNotebooks.isSet())
    {
        for(auto it = syncChunk.expungedNotebooks->constBegin(),
            end = syncChunk.expungedNotebooks->constEnd(); it != end; ++it)
        {
            strm << indent << "expunged notebook guid = " << *it << ";\n";
        }
    }

    if (syncChunk.expungedNotes.isSet())
    {
        for(auto it = syncChunk.expungedNotes->constBegin(),
            end = syncChunk.expungedNotes->constEnd(); it != end; ++it)
        {
            strm << indent << "expunged note guid = " << *it << ";\n";
        }
    }

    if (syncChunk.expungedSearches.isSet())
    {
        for(auto it = syncChunk.expungedSearches->constBegin(),
            end = syncChunk.expungedSearches->constEnd(); it != end; ++it)
        {
            strm << indent << "expunged search guid = " << *it << ";\n";
        }
    }

    if (syncChunk.expungedTags.isSet())
    {
        for(auto it = syncChunk.expungedTags->constBegin(),
            end = syncChunk.expungedTags->constEnd(); it != end; ++it)
        {
            strm << indent << "expunged tag guid = " << *it << ";\n";
        }
    }

    return strm;
}

QTextStream & operator<<(QTextStream & strm, const qevercloud::Tag & tag)
{
    strm << "qevercloud::Tag: {\n";
    const char * indent = "  ";

    PRINT_FIELD(tag, guid);
    PRINT_FIELD(tag, name);
    PRINT_FIELD(tag, parentGuid);
    PRINT_FIELD(tag, updateSequenceNum, QString::number);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SavedSearch & savedSearch)
{
    strm << "qevercloud::SavedSearch: {\n";
    const char * indent = "  ";

    PRINT_FIELD(savedSearch, guid);
    PRINT_FIELD(savedSearch, name);
    PRINT_FIELD(savedSearch, query);
    PRINT_FIELD(savedSearch, format, queryFormatToString);
    PRINT_FIELD(savedSearch, updateSequenceNum, QString::number);

    strm << indent << "SavedSearchScope = ";
    if (savedSearch.scope.isSet())
    {
        strm << "{\n";
        strm << indent << indent << "includeAccount = "
             << (savedSearch.scope->includeAccount.isSet()
                 ? (savedSearch.scope->includeAccount.ref()
                    ? "true"
                    : "false")
                 : "<empty>") << ";\n";
        strm << indent << indent
             << "includePersonalLinkedNotebooks = "
             << (savedSearch.scope->includePersonalLinkedNotebooks.isSet()
                 ? (savedSearch.scope->includePersonalLinkedNotebooks.ref()
                    ? "true"
                    : "false")
                 : "<empty>") << ";\n";
        strm << indent << indent
             << "includeBusinessLinkedNotebooks = "
             << (savedSearch.scope->includeBusinessLinkedNotebooks.isSet()
                 ? (savedSearch.scope->includeBusinessLinkedNotebooks.ref()
                    ? "true"
                    : "false")
                 : "<empty>") << ";\n";
        strm << indent << "};\n";
    }
    else
    {
        strm << "<empty>;\n";
    }

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::LinkedNotebook & linkedNotebook)
{
    strm << "qevercloud::LinkedNotebook: {\n";
    const char * indent = "  ";

    PRINT_FIELD(linkedNotebook, shareName);
    PRINT_FIELD(linkedNotebook, username);
    PRINT_FIELD(linkedNotebook, shardId);
    PRINT_FIELD(linkedNotebook, sharedNotebookGlobalId);
    PRINT_FIELD(linkedNotebook, uri);
    PRINT_FIELD(linkedNotebook, guid);
    PRINT_FIELD(linkedNotebook, updateSequenceNum, QString::number);
    PRINT_FIELD(linkedNotebook, noteStoreUrl);
    PRINT_FIELD(linkedNotebook, webApiUrlPrefix);
    PRINT_FIELD(linkedNotebook, stack);
    PRINT_FIELD(linkedNotebook, businessId, QString::number);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm, const qevercloud::Notebook & notebook)
{
    strm << "qevercloud::Notebook: {\n";
    const char * indent = "  ";

    PRINT_FIELD(notebook, guid);
    PRINT_FIELD(notebook, name);
    PRINT_FIELD(notebook, updateSequenceNum, QString::number);
    PRINT_FIELD(notebook, defaultNotebook, boolToString);
    PRINT_FIELD(notebook, serviceCreated, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(notebook, serviceUpdated, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(notebook, publishing, ToString);
    PRINT_FIELD(notebook, published, boolToString);
    PRINT_FIELD(notebook, stack);

    if (notebook.sharedNotebooks.isSet())
    {
        strm << indent << "sharedNotebooks: {\n";

        for(auto it = notebook.sharedNotebooks->begin(),
            end = notebook.sharedNotebooks->end(); it != end; ++it)
        {
            strm << indent << indent << *it;
        }

        strm << indent << "};\n";
    }

    PRINT_FIELD(notebook, businessNotebook, ToString);
    PRINT_FIELD(notebook, contact, ToString);
    PRINT_FIELD(notebook, restrictions, ToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::Publishing & publishing)
{
    strm << "qevercloud::Publishing: {\n";
    const char * indent = "  ";

    PRINT_FIELD(publishing, uri);
    PRINT_FIELD(publishing, order, noteSortOrderToString);
    PRINT_FIELD(publishing, ascending, boolToString);
    PRINT_FIELD(publishing, publicDescription);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SharedNotebook & sharedNotebook)
{
    strm << "qevercloud::SharedNotebook: {\n";
    const char * indent = "  ";

    PRINT_FIELD(sharedNotebook, id, QString::number);
    PRINT_FIELD(sharedNotebook, userId, QString::number);
    PRINT_FIELD(sharedNotebook, notebookGuid);
    PRINT_FIELD(sharedNotebook, email);
    PRINT_FIELD(sharedNotebook, serviceCreated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(sharedNotebook, serviceUpdated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(sharedNotebook, privilege, sharedNotebookPrivilegeLevelToString);
    PRINT_FIELD(sharedNotebook, recipientSettings, ToString);
    PRINT_FIELD(sharedNotebook, recipientIdentityId, QString::number);
    PRINT_FIELD(sharedNotebook, recipientUsername);
    PRINT_FIELD(sharedNotebook, recipientUserId, QString::number);
    PRINT_FIELD(sharedNotebook, globalId);
    PRINT_FIELD(sharedNotebook, sharerUserId, QString::number);
    PRINT_FIELD(sharedNotebook, serviceAssigned,
                quentier::printableDateTimeFromTimestamp);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::BusinessNotebook & businessNotebook)
{
    strm << "qevercloud::BusinessNotebook: {\n";
    const char * indent = "  ";

    PRINT_FIELD(businessNotebook, notebookDescription);
    PRINT_FIELD(businessNotebook, privilege, sharedNotebookPrivilegeLevelToString);
    PRINT_FIELD(businessNotebook, recommended, boolToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm, const qevercloud::User & user)
{
    strm << "qevercloud::User: {\n";
    const char * indent = "  ";

    PRINT_FIELD(user, id, QString::number);
    PRINT_FIELD(user, username);
    PRINT_FIELD(user, email);
    PRINT_FIELD(user, name);
    PRINT_FIELD(user, timezone);
    PRINT_FIELD(user, privilege, privilegeLevelToString);
    PRINT_FIELD(user, created, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(user, updated, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(user, deleted, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(user, active, boolToString);
    PRINT_FIELD(user, attributes, ToString);
    PRINT_FIELD(user, accounting, ToString);
    PRINT_FIELD(user, businessUserInfo, ToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SharedNotebookRecipientSettings & settings)
{
    strm << "qevercloud::SharedNotebookRecipientSettings: {\n";
    const char * indent = "  ";

    PRINT_FIELD(settings, reminderNotifyEmail, boolToString);
    PRINT_FIELD(settings, reminderNotifyInApp, boolToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::ReminderEmailConfig::type & config)
{
    strm << reminderEmailConfigToString(config);
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::PremiumOrderStatus::type & status)
{
    strm << premiumOrderStatusToString(status);
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::BusinessUserRole::type & role)
{
    strm << businessUserRoleToString(role);
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SponsoredGroupRole::type & role)
{
    strm << "qevercloud::SponsoredGroupRole: ";

    switch (role)
    {
    case qevercloud::SponsoredGroupRole::GROUP_MEMBER:
        strm << "GROUP_MEMBER";
        break;
    case qevercloud::SponsoredGroupRole::GROUP_ADMIN:
        strm << "GROUP_ADMIN";
        break;
    case qevercloud::SponsoredGroupRole::GROUP_OWNER:
        strm << "GROUP_OWNER";
        break;
    default:
        strm << "Unknown";
        break;
    }

    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::NoteRestrictions & restrictions)
{
    strm << "qevercloud::NoteRestrictions {\n";
    const char * indent = "  ";

    PRINT_FIELD(restrictions, noUpdateTitle, boolToString);
    PRINT_FIELD(restrictions, noUpdateContent, boolToString);
    PRINT_FIELD(restrictions, noEmail, boolToString);
    PRINT_FIELD(restrictions, noShare, boolToString);
    PRINT_FIELD(restrictions, noSharePublicly, boolToString);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::NoteLimits & limits)
{
    strm << "qevercloud::NoteLimits {\n";
    const char * indent = "  ";

    PRINT_FIELD(limits, noteResourceCountMax, QString::number);
    PRINT_FIELD(limits, uploadLimit, QString::number);
    PRINT_FIELD(limits, resourceSizeMax, QString::number);
    PRINT_FIELD(limits, noteSizeMax, QString::number);
    PRINT_FIELD(limits, uploaded, QString::number);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm, const qevercloud::Note & note)
{
    strm << "qevercloud::Note {\n";
    const char * indent = "  ";

    PRINT_FIELD(note, guid);
    PRINT_FIELD(note, title);
    PRINT_FIELD(note, content);
    PRINT_FIELD(note, contentHash, byteArrayToHex);
    PRINT_FIELD(note, created, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(note, updated, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(note, deleted, quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(note, active, boolToString);
    PRINT_FIELD(note, updateSequenceNum, QString::number);
    PRINT_FIELD(note, notebookGuid);
    PRINT_LIST_FIELD(note, tagGuids)
    PRINT_LIST_FIELD(note, resources, ToString)
    PRINT_FIELD(note, attributes, ToString);
    PRINT_LIST_FIELD(note, tagNames)
    PRINT_LIST_FIELD(note, sharedNotes)
    PRINT_FIELD(note, restrictions, ToString);
    PRINT_FIELD(note, limits, ToString);

    strm << "};\n";
    return strm;
}

QString sharedNotePrivilegeLevelToString(
    const qevercloud::SharedNotePrivilegeLevel::type level)
{
    switch(level)
    {
    case qevercloud::SharedNotePrivilegeLevel::READ_NOTE:
        return QStringLiteral("READ_NOTE");
    case qevercloud::SharedNotePrivilegeLevel::MODIFY_NOTE:
        return QStringLiteral("MODIFY_NOTE");
    case qevercloud::SharedNotePrivilegeLevel::FULL_ACCESS:
        return QStringLiteral("FULL_ACCESS");
    default:
        return QStringLiteral("Unknown");
    }
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SharedNote & sharedNote)
{
    strm << "qevercloud::SharedNote: {\n";
    const char * indent = "  ";

    PRINT_FIELD(sharedNote, sharerUserID, QString::number);
    PRINT_FIELD(sharedNote, recipientIdentity, ToString);
    PRINT_FIELD(sharedNote, privilege, sharedNotePrivilegeLevelToString);
    PRINT_FIELD(sharedNote, serviceCreated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(sharedNote, serviceUpdated,
                quentier::printableDateTimeFromTimestamp);
    PRINT_FIELD(sharedNote, serviceAssigned,
                quentier::printableDateTimeFromTimestamp);

    strm << "};\n";
    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::EDAMErrorCode::type & obj)
{
    strm << "qevercloud::EDAMErrorCode: ";

    switch(obj)
    {
    case qevercloud::EDAMErrorCode::UNKNOWN:
        strm << "UNKNOWN";
        break;
    case qevercloud::EDAMErrorCode::BAD_DATA_FORMAT:
        strm << "BAD_DATA_FORMAT";
        break;
    case qevercloud::EDAMErrorCode::PERMISSION_DENIED:
        strm << "PERMISSION_DENIED";
        break;
    case qevercloud::EDAMErrorCode::INTERNAL_ERROR:
        strm << "INTERNAL_ERROR";
        break;
    case qevercloud::EDAMErrorCode::DATA_REQUIRED:
        strm << "DATA_REQUIRED";
        break;
    case qevercloud::EDAMErrorCode::LIMIT_REACHED:
        strm << "LIMIT_REACHED";
        break;
    case qevercloud::EDAMErrorCode::QUOTA_REACHED:
        strm << "QUOTA_REACHED";
        break;
    case qevercloud::EDAMErrorCode::INVALID_AUTH:
        strm << "INVALID_AUTH";
        break;
    case qevercloud::EDAMErrorCode::AUTH_EXPIRED:
        strm << "AUTH_EXPIRED";
        break;
    case qevercloud::EDAMErrorCode::DATA_CONFLICT:
        strm << "DATA_CONFLICT";
        break;
    case qevercloud::EDAMErrorCode::ENML_VALIDATION:
        strm << "ENML_VALIDATION";
        break;
    case qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE:
        strm << "SHARD_UNAVAILABLE";
        break;
    case qevercloud::EDAMErrorCode::LEN_TOO_SHORT:
        strm << "LEN_TOO_SHORT";
        break;
    case qevercloud::EDAMErrorCode::LEN_TOO_LONG:
        strm << "LEN_TOO_LONG";
        break;
    case qevercloud::EDAMErrorCode::TOO_FEW:
        strm << "TOO_FEW";
        break;
    case qevercloud::EDAMErrorCode::TOO_MANY:
        strm << "TOO_MANY";
        break;
    case qevercloud::EDAMErrorCode::UNSUPPORTED_OPERATION:
        strm << "UNSUPPORTED_OPERATION";
        break;
    case qevercloud::EDAMErrorCode::TAKEN_DOWN:
        strm << "TAKEN_DOWN";
        break;
    case qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED:
        strm << "RATE_LIMIT_REACHED";
        break;
    default:
        strm << "<unknown>";
        break;
    }

    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SyncState & syncState)
{
    strm << "qevercloud::SyncState {\n"
         << "  current time = "
         << quentier::printableDateTimeFromTimestamp(syncState.currentTime)
         << ";\n"
         << "  full sync before = "
         << quentier::printableDateTimeFromTimestamp(syncState.fullSyncBefore)
         << ";\n"
         << "  update count = "
         << QString::number(syncState.updateCount) << ";\n"
         << "  uploaded = "
         << (syncState.uploaded.isSet()
             ? QString::number(syncState.uploaded.ref())
             : QStringLiteral("<not set>"))
         << ";\n"
         << "  user last updated = "
         << (syncState.userLastUpdated.isSet()
             ? quentier::printableDateTimeFromTimestamp(syncState.userLastUpdated)
             : QStringLiteral("<not set>"))
         << ";\n"
         << "};\n";

    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::SyncChunkFilter & filter)
{
    strm << "qevercloud::SyncChunkFilter {\n"
         << "  include notes = "
         << (filter.includeNotes.isSet()
             ? (filter.includeNotes.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include note resources = "
         << (filter.includeNoteResources.isSet()
             ? (filter.includeNoteResources.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include note attributes = "
         << (filter.includeNoteAttributes.isSet()
             ? (filter.includeNoteAttributes.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include notebooks = "
         << (filter.includeNotebooks.isSet()
             ? (filter.includeNotebooks.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include tags = "
         << (filter.includeTags.isSet()
             ? (filter.includeTags.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include saved searches = "
         << (filter.includeSearches.isSet()
             ? (filter.includeSearches.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include resources = "
         << (filter.includeResources.isSet()
             ? (filter.includeResources.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include linked notebooks = "
         << (filter.includeLinkedNotebooks.isSet()
             ? (filter.includeLinkedNotebooks.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include expunged = "
         << (filter.includeExpunged.isSet()
             ? (filter.includeExpunged.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include note application data full map = "
         << (filter.includeNoteApplicationDataFullMap.isSet()
             ? (filter.includeNoteApplicationDataFullMap.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include resource application data full map = "
         << (filter.includeResourceApplicationDataFullMap.isSet()
             ? (filter.includeResourceApplicationDataFullMap.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  include shared notes = "
         << (filter.includeSharedNotes.isSet()
             ? (filter.includeSharedNotes.ref()
                ? "true"
                : "false")
             : "<not set>")
         << ";\n"
         << "  omit shared notebooks = "
         << (filter.omitSharedNotebooks.isSet()
              ? (filter.omitSharedNotebooks.ref()
                 ? "true"
                 : "false")
              : "<not set>")
         << ";\n"
         << "  require note content class = "
         << (filter.requireNoteContentClass.isSet()
             ? filter.requireNoteContentClass.ref()
             : QStringLiteral("<not set>"))
         << ";\n";

    strm << "  notebook guids: ";
    if (filter.notebookGuids.isSet())
    {
        strm << "\n";
        for(auto it = filter.notebookGuids->constBegin(),
            end = filter.notebookGuids->constEnd(); it != end; ++it)
        {
            strm << "    " << *it << "\n";
        }
    }
    else
    {
        strm << "<not set>;\n";
    }

    return strm;
}

QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::NoteResultSpec & spec)
{
    strm << "qevercloud::NoteResultSpec: {\n"
         << "  include content = "
         << (spec.includeContent.isSet()
             ? (spec.includeContent.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include resources data = "
         << (spec.includeResourcesData.isSet()
             ? (spec.includeResourcesData.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include resources recognition = "
         << (spec.includeResourcesRecognition.isSet()
             ? (spec.includeResourcesRecognition.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include resources alternate data = "
         << (spec.includeResourcesAlternateData.isSet()
             ? (spec.includeResourcesAlternateData.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include shared notes = "
         << (spec.includeSharedNotes.isSet()
             ? (spec.includeSharedNotes.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include note app data values = "
         << (spec.includeNoteAppDataValues.isSet()
             ? (spec.includeNoteAppDataValues.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include resource app data values = "
         << (spec.includeResourceAppDataValues.isSet()
             ? (spec.includeResourceAppDataValues.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n  include account limits = "
         << (spec.includeAccountLimits.isSet()
             ? (spec.includeAccountLimits.ref()
                ? "true"
                : "false")
             : "<not set>")
         << "\n};\n";
    return strm;
}

#if QEVERCLOUD_HAS_OAUTH
QTextStream & operator<<(QTextStream & strm,
                         const qevercloud::EvernoteOAuthWebView::OAuthResult & result)
{
    strm << "qevercloud::EvernoteOAuthWebView::OAuthResult {\n";

    strm << "  noteStoreUrl = " << result.noteStoreUrl << ";\n";
    strm << "  expires = "
         << quentier::printableDateTimeFromTimestamp(result.expires)
         << ";\n";
    strm << "  shardId = " << result.shardId
         << ";\n";
    strm << "  userId = " << QString::number(result.userId)
         << ";\n";
    strm << "  webApiUrlPrefix = " << result.webApiUrlPrefix
         << ";\n";
    strm << "  authenticationToken "
         << (result.authenticationToken.isEmpty()
             ? "is empty"
             : "is not empty") << ";\n";

    strm << "};\n";
    return strm;
}
#endif
