/*
 * Copyright 2016 Dmitry Ivanov
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

#include "data/NotebookData.h"
#include <quentier/types/Notebook.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/User.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

QN_DEFINE_LOCAL_UID(Notebook)
QN_DEFINE_DIRTY(Notebook)
QN_DEFINE_LOCAL(Notebook)
QN_DEFINE_FAVORITED(Notebook)

Notebook::Notebook() :
    d(new NotebookData)
{}

Notebook::Notebook(const Notebook & other) :
    d(other.d)
{}

Notebook::Notebook(Notebook && other) :
    d(std::move(other.d))
{}

Notebook::Notebook(const qevercloud::Notebook & other) :
    d(new NotebookData(other))
{}

Notebook::Notebook(qevercloud::Notebook && other) :
    d(new NotebookData(std::move(other)))
{}

Notebook & Notebook::operator=(const Notebook & other)
{
    if (this != &other) {
        d = other.d;
    }

    return *this;
}

Notebook & Notebook::operator=(Notebook && other)
{
    if (this != &other) {
        d = other.d;
    }

    return *this;
}

Notebook & Notebook::operator=(const qevercloud::Notebook & other)
{
    d = new NotebookData(other);
    return *this;
}

Notebook & Notebook::operator=(qevercloud::Notebook && other)
{
    d = new NotebookData(std::move(other));
    return *this;
}

Notebook::~Notebook()
{}

bool Notebook::operator==(const Notebook & other) const
{
    if (isFavorited() != other.isFavorited()) {
        return false;
    }
    else if (isLocal() != other.isLocal()) {
        return false;
    }
    else if (isDirty() != other.isDirty()) {
        return false;
    }
    else if (d == other.d) {
        return true;
    }
    else {
        return (*d == *(other.d));
    }
}

bool Notebook::operator!=(const Notebook & other) const
{
    return !(*this == other);
}

Notebook::operator const qevercloud::Notebook &() const
{
    return d->m_qecNotebook;
}

Notebook::operator qevercloud::Notebook&()
{
    return d->m_qecNotebook;
}

void Notebook::clear()
{
    d->m_qecNotebook = qevercloud::Notebook();
}

bool Notebook::validateName(const QString & name, QNLocalizedString * pErrorDescription)
{
    if (name != name.trimmed())
    {
        if (pErrorDescription) {
            *pErrorDescription = QT_TR_NOOP("notebook name cannot start or end with whitespace");
        }

        return false;
    }

    int len = name.length();
    if (len < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN)
    {
        if (pErrorDescription) {
            *pErrorDescription = QT_TR_NOOP("notebook name's length is too small");
        }

        return false;
    }

    if (len > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX)
    {
        if (pErrorDescription) {
            *pErrorDescription = QT_TR_NOOP("notebook name's length is too large");
        }

        return false;
    }

    return true;
}

bool Notebook::hasGuid() const
{
    return d->m_qecNotebook.guid.isSet();
}

const QString & Notebook::guid() const
{
    return d->m_qecNotebook.guid;
}

void Notebook::setGuid(const QString & guid)
{
    if (!guid.isEmpty()) {
        d->m_qecNotebook.guid = guid;
    }
    else {
        d->m_qecNotebook.guid.clear();
    }
}

bool Notebook::hasUpdateSequenceNumber() const
{
    return d->m_qecNotebook.updateSequenceNum.isSet();
}

qint32 Notebook::updateSequenceNumber() const
{
    return d->m_qecNotebook.updateSequenceNum;
}

void Notebook::setUpdateSequenceNumber(const qint32 usn)
{
    d->m_qecNotebook.updateSequenceNum = usn;
}

bool Notebook::checkParameters(QNLocalizedString & errorDescription) const
{
    if (localUid().isEmpty() && !d->m_qecNotebook.guid.isSet()) {
        errorDescription = QT_TR_NOOP("both notebook's local and remote guids are not set");
        return false;
    }

    return d->checkParameters(errorDescription);
}

bool Notebook::hasName() const
{
    return d->m_qecNotebook.name.isSet();
}

const QString & Notebook::name() const
{
    return d->m_qecNotebook.name;
}

void Notebook::setName(const QString & name)
{
    if (!canUpdateNotebook()) {
        QNDEBUG("Can't set name for notebook: updating is forbidden");
        return;
    }

    if (!name.isEmpty()) {
        d->m_qecNotebook.name = name;
    }
    else {
        d->m_qecNotebook.name.clear();
    }
}

bool Notebook::isDefaultNotebook() const
{
    return d->m_qecNotebook.defaultNotebook.isSet() && d->m_qecNotebook.defaultNotebook;
}

void Notebook::setDefaultNotebook(const bool defaultNotebook)
{
    if (!canSetDefaultNotebook()) {
        QNDEBUG("Can't set defaut notebook flag: setting default notebook is forbidden");
        return;
    }

    d->m_qecNotebook.defaultNotebook = defaultNotebook;
}

bool Notebook::hasLinkedNotebookGuid() const
{
    return d->m_linkedNotebookGuid.isSet();
}

const QString & Notebook::linkedNotebookGuid() const
{
    return d->m_linkedNotebookGuid.ref();
}

void Notebook::setLinkedNotebookGuid(const QString & linkedNotebookGuid)
{
    if (!linkedNotebookGuid.isEmpty()) {
        d->m_linkedNotebookGuid = linkedNotebookGuid;
    }
    else {
        d->m_linkedNotebookGuid.clear();
    }
}

bool Notebook::hasCreationTimestamp() const
{
    return d->m_qecNotebook.serviceCreated.isSet();
}

qint64 Notebook::creationTimestamp() const
{
    return d->m_qecNotebook.serviceCreated;
}

void Notebook::setCreationTimestamp(const qint64 timestamp)
{
    d->m_qecNotebook.serviceCreated = timestamp;
}

bool Notebook::hasModificationTimestamp() const
{
    return d->m_qecNotebook.serviceUpdated.isSet();
}

qint64 Notebook::modificationTimestamp() const
{
    return d->m_qecNotebook.serviceUpdated;
}

void Notebook::setModificationTimestamp(const qint64 timestamp)
{
    if (!canUpdateNotebook()) {
        QNDEBUG("Can't set modification timestamp for notebook: updating is forbidden");
        return;
    }

    d->m_qecNotebook.serviceUpdated = timestamp;
}

bool Notebook::hasPublishingUri() const
{
    return d->m_qecNotebook.publishing.isSet() && d->m_qecNotebook.publishing->uri.isSet();
}

const QString & Notebook::publishingUri() const
{
    return d->m_qecNotebook.publishing->uri;
}

#define CHECK_AND_SET_PUBLISHING \
    if (!d->m_qecNotebook.publishing.isSet()) { \
        d->m_qecNotebook.publishing = qevercloud::Publishing(); \
    } \
    if (!d->m_qecNotebook.published.isSet()) { \
        d->m_qecNotebook.published = true; \
    }

void Notebook::setPublishingUri(const QString & uri)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set publishing uri for notebook: update is forbidden"));
        return;
    }

    if (!uri.isEmpty()) {
        CHECK_AND_SET_PUBLISHING;
    }

    d->m_qecNotebook.publishing->uri = uri;
}

bool Notebook::hasPublishingOrder() const
{
    return d->m_qecNotebook.publishing.isSet() && d->m_qecNotebook.publishing->order.isSet();
}

qint8 Notebook::publishingOrder() const
{
    return static_cast<qint8>(d->m_qecNotebook.publishing->order);
}

void Notebook::setPublishingOrder(const qint8 order)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set publishing order for notebook: update is forbidden"));
        return;
    }

    if (order <= static_cast<qint8>(qevercloud::NoteSortOrder::TITLE)) {
        CHECK_AND_SET_PUBLISHING;
        d->m_qecNotebook.publishing->order = static_cast<qevercloud::NoteSortOrder::type>(order);
    }
    else if (d->m_qecNotebook.publishing.isSet()) {
        d->m_qecNotebook.publishing->order.clear();
    }
}

bool Notebook::hasPublishingAscending() const
{
    return d->m_qecNotebook.publishing.isSet() && d->m_qecNotebook.publishing->ascending.isSet();
}

bool Notebook::isPublishingAscending() const
{
    return d->m_qecNotebook.publishing->ascending;
}

void Notebook::setPublishingAscending(const bool ascending)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set publishing ascending for notebook: update is forbidden"));
        return;
    }

    CHECK_AND_SET_PUBLISHING;
    d->m_qecNotebook.publishing->ascending = ascending;
}

bool Notebook::hasPublishingPublicDescription() const
{
    return d->m_qecNotebook.publishing.isSet() && d->m_qecNotebook.publishing->publicDescription.isSet();
}

const QString & Notebook::publishingPublicDescription() const
{
    return d->m_qecNotebook.publishing->publicDescription;
}

void Notebook::setPublishingPublicDescription(const QString & publicDescription)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set publishing public description for notebook: update is forbidden"));
        return;
    }

    if (!publicDescription.isEmpty()) {
        CHECK_AND_SET_PUBLISHING;
    }

    d->m_qecNotebook.publishing->publicDescription = publicDescription;
}

#undef CHECK_AND_SET_PUBLISHING

bool Notebook::hasPublished() const
{
    return d->m_qecNotebook.published.isSet();
}

bool Notebook::isPublished() const
{
    return d->m_qecNotebook.published.isSet();
}

void Notebook::setPublished(const bool published)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set published flag for notebook: update is forbidden"));
        return;
    }

    d->m_qecNotebook.published = published;
}

bool Notebook::hasStack() const
{
    return d->m_qecNotebook.stack.isSet();
}

const QString & Notebook::stack() const
{
    return d->m_qecNotebook.stack;
}

void Notebook::setStack(const QString & stack)
{
    if (!canSetNotebookStack()) {
        QNDEBUG(QStringLiteral("Can't set stack for notebook: setting stack is forbidden"));
        return;
    }

    if (!stack.isEmpty()) {
        d->m_qecNotebook.stack = stack;
    }
    else {
        d->m_qecNotebook.stack.clear();
    }
}

bool Notebook::hasSharedNotebooks()
{
    return d->m_qecNotebook.sharedNotebooks.isSet();
}

QList<SharedNotebook> Notebook::sharedNotebooks() const
{
    QList<SharedNotebook> notebooks;

    if (!d->m_qecNotebook.sharedNotebooks.isSet()) {
        return notebooks;
    }

    const QList<qevercloud::SharedNotebook> & sharedNotebooks = d->m_qecNotebook.sharedNotebooks;
    int numSharedNotebooks = sharedNotebooks.size();
    notebooks.reserve(qMax(numSharedNotebooks, 0));
    for(int i = 0; i < numSharedNotebooks; ++i) {
        const qevercloud::SharedNotebook & qecSharedNotebook = sharedNotebooks[i];
        SharedNotebook sharedNotebook(qecSharedNotebook);
        notebooks << sharedNotebook;
        notebooks.back().setIndexInNotebook(i);
    }

    return notebooks;
}

void Notebook::setSharedNotebooks(QList<qevercloud::SharedNotebook> sharedNotebooks)
{
    d->m_qecNotebook.sharedNotebooks = sharedNotebooks;
}

void Notebook::setSharedNotebooks(QList<SharedNotebook> && notebooks)
{
    if (!d->m_qecNotebook.sharedNotebooks.isSet()) {
        d->m_qecNotebook.sharedNotebooks = QList<qevercloud::SharedNotebook>();
    }

    d->m_qecNotebook.sharedNotebooks->clear();
    int numNotebooks = notebooks.size();
    for(int i = 0; i < numNotebooks; ++i) {
        const SharedNotebook & sharedNotebook = notebooks[i];
        d->m_qecNotebook.sharedNotebooks.ref() << static_cast<const qevercloud::SharedNotebook>(sharedNotebook);
    }
}

void Notebook::addSharedNotebook(const SharedNotebook & sharedNotebook)
{
    if (!d->m_qecNotebook.sharedNotebooks.isSet()) {
        d->m_qecNotebook.sharedNotebooks = QList<qevercloud::SharedNotebook>();
    }

    QList<qevercloud::SharedNotebook> & sharedNotebooks = d->m_qecNotebook.sharedNotebooks;
    const auto & enSharedNotebook = static_cast<const qevercloud::SharedNotebook&>(sharedNotebook);

    if (sharedNotebooks.indexOf(enSharedNotebook) != -1) {
        QNDEBUG(QStringLiteral("Can't add shared notebook: this shared notebook already exists within the notebook"));
        return;
    }

    sharedNotebooks << enSharedNotebook;
}

void Notebook::removeSharedNotebook(const SharedNotebook & sharedNotebook)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't remove shared notebook from notebook: updating is forbidden"));
        return;
    }

    auto & sharedNotebooks = d->m_qecNotebook.sharedNotebooks;
    const auto & enSharedNotebook = static_cast<const qevercloud::SharedNotebook>(sharedNotebook);

    int index = sharedNotebooks->indexOf(enSharedNotebook);
    if (index == -1) {
        QNDEBUG(QStringLiteral("Can't remove shared notebook: this shared notebook doesn't exists within the notebook"));
        return;
    }

    sharedNotebooks->removeAt(index);
}

bool Notebook::hasBusinessNotebookDescription() const
{
    return d->m_qecNotebook.businessNotebook.isSet() && d->m_qecNotebook.businessNotebook->notebookDescription.isSet();
}

const QString & Notebook::businessNotebookDescription() const
{
    return d->m_qecNotebook.businessNotebook->notebookDescription;
}

#define CHECK_AND_SET_BUSINESS_NOTEBOOK \
    if (!d->m_qecNotebook.businessNotebook.isSet()) { \
        d->m_qecNotebook.businessNotebook = qevercloud::BusinessNotebook(); \
    }

void Notebook::setBusinessNotebookDescription(const QString & businessNotebookDescription)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set business notebook description for notebook: updating is forbidden"));
        return;
    }

    if (!businessNotebookDescription.isEmpty()) {
        CHECK_AND_SET_BUSINESS_NOTEBOOK;
    }

    d->m_qecNotebook.businessNotebook->notebookDescription = businessNotebookDescription;
}

bool Notebook::hasBusinessNotebookPrivilegeLevel() const
{
    return d->m_qecNotebook.businessNotebook.isSet() && d->m_qecNotebook.businessNotebook->privilege.isSet();
}

qint8 Notebook::businessNotebookPrivilegeLevel() const
{
    return static_cast<qint8>(d->m_qecNotebook.businessNotebook->privilege);
}

void Notebook::setBusinessNotebookPrivilegeLevel(const qint8 privilegeLevel)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set business notebook privilege level for notebook: updating is forbidden"));
        return;
    }

    if (privilegeLevel <= static_cast<qint8>(qevercloud::SharedNotebookPrivilegeLevel::BUSINESS_FULL_ACCESS)) {
        CHECK_AND_SET_BUSINESS_NOTEBOOK;
        d->m_qecNotebook.businessNotebook->privilege = static_cast<qevercloud::SharedNotebookPrivilegeLevel::type>(privilegeLevel);
    }
    else if (d->m_qecNotebook.businessNotebook.isSet()) {
        d->m_qecNotebook.businessNotebook->privilege.clear();
    }
}

bool Notebook::hasBusinessNotebookRecommended() const
{
    return d->m_qecNotebook.businessNotebook.isSet() && d->m_qecNotebook.businessNotebook->recommended.isSet();
}

bool Notebook::isBusinessNotebookRecommended() const
{
    return d->m_qecNotebook.businessNotebook->recommended;
}

void Notebook::setBusinessNotebookRecommended(const bool recommended)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set business notebook recommended flag for notebook: updating is forbidden"));
        return;
    }

    CHECK_AND_SET_BUSINESS_NOTEBOOK;
    d->m_qecNotebook.businessNotebook->recommended = recommended;
}

#undef CHECK_AND_SET_BUSINESS_NOTEBOOK

bool Notebook::hasContact() const
{
    return d->m_qecNotebook.contact.isSet();
}

const User Notebook::contact() const
{
    return User(d->m_qecNotebook.contact.ref());
}

void Notebook::setContact(const User & contact)
{
    if (!canUpdateNotebook()) {
        QNDEBUG(QStringLiteral("Can't set contact for notebook: updating is forbidden"));
        return;
    }

    d->m_qecNotebook.contact = static_cast<const qevercloud::User&>(contact);
}

bool Notebook::isLastUsed() const
{
    return d->m_isLastUsed;
}

void Notebook::setLastUsed(const bool lastUsed)
{
    d->m_isLastUsed = lastUsed;
}

bool Notebook::canReadNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noReadNotes.isSet() &&
             d->m_qecNotebook.restrictions->noReadNotes);
}

#define CHECK_AND_SET_NOTEBOOK_RESTRICTIONS \
    if (!d->m_qecNotebook.restrictions.isSet()) { \
        d->m_qecNotebook.restrictions = qevercloud::NotebookRestrictions(); \
    }

void Notebook::setCanReadNotes(const bool canReadNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noReadNotes = !canReadNotes;
}

bool Notebook::canCreateNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noCreateNotes.isSet() &&
             d->m_qecNotebook.restrictions->noCreateNotes);
}

void Notebook::setCanCreateNotes(const bool canCreateNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noCreateNotes = !canCreateNotes;
}

bool Notebook::canUpdateNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noUpdateNotes.isSet() &&
             d->m_qecNotebook.restrictions->noUpdateNotes);
}

void Notebook::setCanUpdateNotes(const bool canUpdateNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noUpdateNotes = !canUpdateNotes;
}

bool Notebook::canExpungeNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noExpungeNotes.isSet() &&
             d->m_qecNotebook.restrictions->noExpungeNotes);
}

void Notebook::setCanExpungeNotes(const bool canExpungeNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noExpungeNotes = !canExpungeNotes;
}

bool Notebook::canShareNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noShareNotes.isSet() &&
             d->m_qecNotebook.restrictions->noShareNotes);
}

void Notebook::setCanShareNotes(const bool canShareNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noShareNotes = !canShareNotes;
}

bool Notebook::canEmailNotes() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noEmailNotes.isSet() &&
             d->m_qecNotebook.restrictions->noEmailNotes);
}

void Notebook::setCanEmailNotes(const bool canEmailNotes)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noEmailNotes = !canEmailNotes;
}

bool Notebook::canSendMessageToRecipients() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noSendMessageToRecipients.isSet() &&
             d->m_qecNotebook.restrictions->noSendMessageToRecipients);
}

void Notebook::setCanSendMessageToRecipients(const bool canSendMessageToRecipients)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noSendMessageToRecipients = !canSendMessageToRecipients;
}

bool Notebook::canUpdateNotebook() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noUpdateNotebook.isSet() &&
             d->m_qecNotebook.restrictions->noUpdateNotebook);
}

void Notebook::setCanUpdateNotebook(const bool canUpdateNotebook)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noUpdateNotebook = !canUpdateNotebook;
}

bool Notebook::canExpungeNotebook() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noExpungeNotebook.isSet() &&
             d->m_qecNotebook.restrictions->noExpungeNotebook);
}

void Notebook::setCanExpungeNotebook(const bool canExpungeNotebook)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noExpungeNotebook = !canExpungeNotebook;
}

bool Notebook::canSetDefaultNotebook() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noSetDefaultNotebook.isSet() &&
             d->m_qecNotebook.restrictions->noSetDefaultNotebook);
}

void Notebook::setCanSetDefaultNotebook(const bool canSetDefaultNotebook)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noSetDefaultNotebook = !canSetDefaultNotebook;
}

bool Notebook::canSetNotebookStack() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noSetNotebookStack.isSet() &&
             d->m_qecNotebook.restrictions->noSetNotebookStack);
}

void Notebook::setCanSetNotebookStack(const bool canSetNotebookStack)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noSetNotebookStack = !canSetNotebookStack;
}

bool Notebook::canPublishToPublic() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noPublishToPublic.isSet() &&
             d->m_qecNotebook.restrictions->noPublishToPublic);
}

void Notebook::setCanPublishToPublic(const bool canPublishToPublic)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noPublishToPublic = !canPublishToPublic;
}

bool Notebook::canPublishToBusinessLibrary() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noPublishToBusinessLibrary.isSet() &&
             d->m_qecNotebook.restrictions->noPublishToBusinessLibrary);
}

void Notebook::setCanPublishToBusinessLibrary(const bool canPublishToBusinessLibrary)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noPublishToBusinessLibrary = !canPublishToBusinessLibrary;
}

bool Notebook::canCreateTags() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noCreateTags.isSet() &&
             d->m_qecNotebook.restrictions->noCreateTags);
}

void Notebook::setCanCreateTags(const bool canCreateTags)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noCreateTags = !canCreateTags;
}

bool Notebook::canUpdateTags() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noUpdateTags.isSet() &&
             d->m_qecNotebook.restrictions->noUpdateTags);
}

void Notebook::setCanUpdateTags(const bool canUpdateTags)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noUpdateTags = !canUpdateTags;
}

bool Notebook::canExpungeTags() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noExpungeTags.isSet() &&
             d->m_qecNotebook.restrictions->noExpungeTags);
}

void Notebook::setCanExpungeTags(const bool canExpungeTags)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noExpungeTags = !canExpungeTags;
}

bool Notebook::canSetParentTag() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noSetParentTag.isSet() &&
             d->m_qecNotebook.restrictions->noSetParentTag);
}

void Notebook::setCanSetParentTag(const bool canSetParentTag)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noSetParentTag = !canSetParentTag;
}

bool Notebook::canCreateSharedNotebooks() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noCreateSharedNotebooks.isSet() &&
             d->m_qecNotebook.restrictions->noCreateSharedNotebooks);
}

void Notebook::setCanCreateSharedNotebooks(const bool canCreateSharedNotebooks)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noCreateSharedNotebooks = !canCreateSharedNotebooks;
}

bool Notebook::canShareNotesWithBusiness() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noShareNotesWithBusiness.isSet() &&
             d->m_qecNotebook.restrictions->noShareNotesWithBusiness);
}

void Notebook::setCanShareNotesWithBusiness(const bool canShareNotesWithBusiness)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noShareNotesWithBusiness = !canShareNotesWithBusiness;
}

bool Notebook::canRenameNotebook() const
{
    return !(d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->noRenameNotebook.isSet() &&
             d->m_qecNotebook.restrictions->noRenameNotebook);
}

void Notebook::setCanRenameNotebook(const bool canRenameNotebook)
{
    CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
    d->m_qecNotebook.restrictions->noRenameNotebook = !canRenameNotebook;
}

bool Notebook::hasUpdateWhichSharedNotebookRestrictions() const
{
    return d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->updateWhichSharedNotebookRestrictions.isSet();
}

qint8 Notebook::updateWhichSharedNotebookRestrictions() const
{
    return static_cast<qint8>(d->m_qecNotebook.restrictions->updateWhichSharedNotebookRestrictions);
}

void Notebook::setUpdateWhichSharedNotebookRestrictions(const qint8 which)
{
    if (which <= static_cast<qint8>(qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS)) {
        CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
        d->m_qecNotebook.restrictions->updateWhichSharedNotebookRestrictions = static_cast<qevercloud::SharedNotebookInstanceRestrictions::type>(which);
    }
    else if (d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->updateWhichSharedNotebookRestrictions.isSet()) {
        d->m_qecNotebook.restrictions->updateWhichSharedNotebookRestrictions.clear();
    }
}

bool Notebook::hasExpungeWhichSharedNotebookRestrictions() const
{
    return d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->expungeWhichSharedNotebookRestrictions.isSet();
}

qint8 Notebook::expungeWhichSharedNotebookRestrictions() const
{
    return static_cast<qint8>(d->m_qecNotebook.restrictions->expungeWhichSharedNotebookRestrictions);
}

void Notebook::setExpungeWhichSharedNotebookRestrictions(const qint8 which)
{
    if (which <= static_cast<qint8>(qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS)) {
        CHECK_AND_SET_NOTEBOOK_RESTRICTIONS;
        d->m_qecNotebook.restrictions->expungeWhichSharedNotebookRestrictions = static_cast<qevercloud::SharedNotebookInstanceRestrictions::type>(which);
    }
    else if (d->m_qecNotebook.restrictions.isSet() && d->m_qecNotebook.restrictions->expungeWhichSharedNotebookRestrictions.isSet()) {
        d->m_qecNotebook.restrictions->expungeWhichSharedNotebookRestrictions.clear();
    }
}

#undef CHECK_AND_SET_NOTEBOOK_RESTRICTIONS

bool Notebook::hasRestrictions() const
{
    return d->m_qecNotebook.restrictions.isSet();
}

const qevercloud::NotebookRestrictions & Notebook::restrictions() const
{
    return d->m_qecNotebook.restrictions;
}

void Notebook::setNotebookRestrictions(qevercloud::NotebookRestrictions && restrictions)
{
    d->m_qecNotebook.restrictions = std::move(restrictions);
}

bool Notebook::hasRecipientReminderNotifyEmail() const
{
    if (!d->m_qecNotebook.recipientSettings.isSet()) {
        return false;
    }

    return d->m_qecNotebook.recipientSettings->reminderNotifyEmail.isSet();
}

bool Notebook::recipientReminderNotifyEmail() const
{
    return d->m_qecNotebook.recipientSettings->reminderNotifyEmail;
}

#define CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS \
    if (!d->m_qecNotebook.recipientSettings.isSet()) { \
        d->m_qecNotebook.recipientSettings = qevercloud::NotebookRecipientSettings(); \
    }

void Notebook::setRecipientReminderNotifyEmail(const bool notifyEmail)
{
    CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS
    d->m_qecNotebook.recipientSettings->reminderNotifyEmail = notifyEmail;
}

bool Notebook::hasRecipientReminderNotifyInApp() const
{
    if (!d->m_qecNotebook.recipientSettings.isSet()) {
        return false;
    }

    return d->m_qecNotebook.recipientSettings->reminderNotifyInApp.isSet();
}

bool Notebook::recipientReminderNotifyInApp() const
{
    return d->m_qecNotebook.recipientSettings->reminderNotifyInApp;
}

void Notebook::setRecipientReminderNotifyInApp(const bool notifyInApp)
{
    CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS
    d->m_qecNotebook.recipientSettings->reminderNotifyInApp = notifyInApp;
}

bool Notebook::hasRecipientInMyList() const
{
    if (!d->m_qecNotebook.recipientSettings.isSet()) {
        return false;
    }

    return d->m_qecNotebook.recipientSettings->inMyList.isSet();
}

bool Notebook::recipientInMyList() const
{
    return d->m_qecNotebook.recipientSettings->inMyList;
}

void Notebook::setRecipientInMyList(const bool inMyList)
{
    CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS
    d->m_qecNotebook.recipientSettings->inMyList = inMyList;
}

bool Notebook::hasRecipientStack() const
{
    if (!d->m_qecNotebook.recipientSettings.isSet()) {
        return false;
    }

    return d->m_qecNotebook.recipientSettings->stack.isSet();
}

const QString & Notebook::recipientStack() const
{
    return d->m_qecNotebook.recipientSettings->stack;
}

void Notebook::setRecipientStack(const QString & recipientStack)
{
    if (recipientStack.isEmpty())
    {
        if (!d->m_qecNotebook.recipientSettings.isSet()) {
            return;
        }

        d->m_qecNotebook.recipientSettings->stack.clear();
    }
    else
    {
        CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS
        d->m_qecNotebook.recipientSettings->stack = recipientStack;
    }
}

#undef CHECK_AND_SET_NOTEBOOK_RECIPIENT_SETTINGS

bool Notebook::hasRecipientSettings() const
{
    return d->m_qecNotebook.recipientSettings.isSet();
}

const qevercloud::NotebookRecipientSettings & Notebook::recipientSettings() const
{
    return d->m_qecNotebook.recipientSettings;
}

void Notebook::setNotebookRecipientSettings(qevercloud::NotebookRecipientSettings && settings)
{
    d->m_qecNotebook.recipientSettings = std::move(settings);
}

QTextStream & Notebook::print(QTextStream & strm) const
{
    strm << QStringLiteral("Notebook {\n");

    strm << QStringLiteral("  local uid: ") << d->m_localUid.toString() << QStringLiteral(";\n");
    strm << QStringLiteral("  is last used: ") << (d->m_isLastUsed ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("  ;\n");

    strm << QStringLiteral("  linked notebook guid: ") << (d->m_linkedNotebookGuid.isSet()
                                                           ? d->m_linkedNotebookGuid.ref()
                                                           : QStringLiteral("<empty>")) << QStringLiteral(";\n");

    strm << QStringLiteral("  dirty: ")     << (isDirty()     ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(";\n");
    strm << QStringLiteral("  local: ")     << (isLocal()     ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(";\n");
    strm << QStringLiteral("  last used: ") << (isLastUsed()  ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(";\n");
    strm << QStringLiteral("  favorited: ") << (isFavorited() ? QStringLiteral("true") : QStringLiteral("false")) << QStringLiteral(";\n");

    strm << d->m_qecNotebook;

    strm << QStringLiteral("};\n");
    return strm;
}

} // namespace quentier
