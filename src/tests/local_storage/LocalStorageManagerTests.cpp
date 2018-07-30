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

#include "LocalStorageManagerTests.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Resource.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/User.h>
#include <quentier/utility/Utility.h>
#include <quentier/utility/UidGenerator.h>
#include <QCryptographicHash>
#include <string>

namespace quentier {
namespace test {

bool TestSavedSearchAddFindUpdateExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    SavedSearch search;
    search.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    search.setUpdateSequenceNumber(1);
    search.setName(QStringLiteral("Fake saved search name"));
    search.setQuery(QStringLiteral("Fake saved search query"));
    search.setQueryFormat(1);
    search.setIncludeAccount(true);
    search.setIncludeBusinessLinkedNotebooks(false);
    search.setIncludePersonalLinkedNotebooks(true);

    ErrorString errorMessage;

    if (!search.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid SavedSearch: ") << search << QStringLiteral(", error: ") << errorDescription);
        return false;
    }

    // ======== Check Add + Find ============
    bool res = localStorageManager.addSavedSearch(search, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const QString searchGuid = search.localUid();
    SavedSearch foundSearch;
    foundSearch.setLocalUid(searchGuid);
    res = localStorageManager.findSavedSearch(foundSearch, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (search != foundSearch) {
        errorDescription = QStringLiteral("Added and found saved searches in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": SavedSearch added to LocalStorageManager: ") << search
                  << QStringLiteral("\nSavedSearch found in LocalStorageManager: ") << foundSearch);
        return false;
    }

    // ========= Check Find by name =============
    SavedSearch foundByNameSearch;
    foundByNameSearch.unsetLocalUid();
    foundByNameSearch.setName(search.name());
    res = localStorageManager.findSavedSearch(foundByNameSearch, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (search != foundByNameSearch) {
        errorDescription = QStringLiteral("Added and found by name saved searches in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": SavedSearch added to LocalStorageManager: ") << search
                  << QStringLiteral("\nSaved search found by name in LocalStorageManager: ") << foundByNameSearch);
        return false;
    }

    // ========= Check Update + Find =============
    SavedSearch modifiedSearch(search);
    modifiedSearch.setUpdateSequenceNumber(search.updateSequenceNumber() + 1);
    modifiedSearch.setName(search.name() + QStringLiteral("_modified"));
    modifiedSearch.setQuery(search.query() + QStringLiteral("_modified"));
    modifiedSearch.setFavorited(true);
    modifiedSearch.setDirty(true);

    QString localUid = modifiedSearch.localUid();
    modifiedSearch.unsetLocalUid();

    res = localStorageManager.updateSavedSearch(modifiedSearch, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findSavedSearch(foundSearch, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    modifiedSearch.setLocalUid(localUid);
    if (modifiedSearch != foundSearch) {
        errorDescription = QStringLiteral("Updated and found saved searches in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": SavedSearch updated in LocalStorageManager: ") << modifiedSearch
                  << QStringLiteral("\nSavedSearch found in LocalStorageManager: ") << foundSearch);
        return false;
    }

    // ========== Check savedSearchCount to return 1 ============
    int count = localStorageManager.savedSearchCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("GetSavedSearchCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ============ Check Expunge + Find (failure expected) ============
    res = localStorageManager.expungeSavedSearch(modifiedSearch, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findSavedSearch(foundSearch, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found saved search which should have been expunged from local storage");
        QNWARNING(errorDescription << QStringLiteral(": SavedSearch expunged from LocalStorageManager: ") << modifiedSearch
                  << QStringLiteral("\nSavedSearch found in LocalStorageManager: ") << foundSearch);
        return false;
    }

    // ========== Check savedSearchCount to return 0 ============
    count = localStorageManager.savedSearchCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 0) {
        errorDescription = QStringLiteral("savedSearchCount returned result different from the expected one (0): ");
        errorDescription += QString::number(count);
        return false;
    }

    return true;
}

bool TestLinkedNotebookAddFindUpdateExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    linkedNotebook.setUpdateSequenceNumber(1);
    linkedNotebook.setShareName(QStringLiteral("Fake linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Fake linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Fake linked notebook shard id"));
    linkedNotebook.setSharedNotebookGlobalId(QStringLiteral("Fake linked notebook shared notebook global id"));
    linkedNotebook.setUri(QStringLiteral("Fake linked notebook uri"));
    linkedNotebook.setNoteStoreUrl(QStringLiteral("Fake linked notebook note store url"));
    linkedNotebook.setWebApiUrlPrefix(QStringLiteral("Fake linked notebook web api url prefix"));
    linkedNotebook.setStack(QStringLiteral("Fake linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    ErrorString errorMessage;

    if (!linkedNotebook.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid LinkedNotebook: ") << linkedNotebook << QStringLiteral(", error: ") << errorDescription);
        return false;
    }

    // ========== Check Add + Find ===========
    bool res = localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const QString linkedNotebookGuid = linkedNotebook.guid();
    LinkedNotebook foundLinkedNotebook;
    foundLinkedNotebook.setGuid(linkedNotebookGuid);
    res = localStorageManager.findLinkedNotebook(foundLinkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (linkedNotebook != foundLinkedNotebook) {
        errorDescription = QStringLiteral("Added and found linked noteboks in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": LinkedNotebook added to LocalStorageManager: ") << linkedNotebook
                  << QStringLiteral("\nLinkedNotebook found in LocalStorageManager: ") << foundLinkedNotebook);
        return false;
    }

    // =========== Check Update + Find ===========
    LinkedNotebook modifiedLinkedNotebook(linkedNotebook);
    modifiedLinkedNotebook.setUpdateSequenceNumber(linkedNotebook.updateSequenceNumber() + 1);
    modifiedLinkedNotebook.setShareName(linkedNotebook.shareName() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setUsername(linkedNotebook.username() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setShardId(linkedNotebook.shardId() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setSharedNotebookGlobalId(linkedNotebook.sharedNotebookGlobalId() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setUri(linkedNotebook.uri() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setNoteStoreUrl(linkedNotebook.noteStoreUrl() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setWebApiUrlPrefix(linkedNotebook.webApiUrlPrefix() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setStack(linkedNotebook.stack() + QStringLiteral("_modified"));
    modifiedLinkedNotebook.setBusinessId(linkedNotebook.businessId() + 1);

    res = localStorageManager.updateLinkedNotebook(modifiedLinkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findLinkedNotebook(foundLinkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (modifiedLinkedNotebook != foundLinkedNotebook) {
        errorDescription = QStringLiteral("Updated and found linked notebooks in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": LinkedNotebook updated in LocalStorageManager: ") << modifiedLinkedNotebook
                  << QStringLiteral("\nLinkedNotebook found in LocalStorageManager: ") << foundLinkedNotebook);
        return false;
    }

    // ========== Check linkedNotebookCount to return 1 ============
    int count = localStorageManager.linkedNotebookCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("linkedNotebookCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ============= Check Expunge + Find (failure expected) ============
    res = localStorageManager.expungeLinkedNotebook(modifiedLinkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findLinkedNotebook(foundLinkedNotebook, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found linked notebook which should have been expunged from local storage");
        QNWARNING(errorDescription << QStringLiteral(": LinkedNotebook expunged from LocalStorageManager: ") << modifiedLinkedNotebook
                  << QStringLiteral("\nLinkedNotebook found in LocalStorageManager: ") << foundLinkedNotebook);
        return false;
    }

    // ========== Check linkedNotebookCount to return 0 ============
    count = localStorageManager.linkedNotebookCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 0) {
        errorDescription = QStringLiteral("GetLinkedNotebookCount returned result different from the expected one (0): ");
        errorDescription += QString::number(count);
        return false;
    }

    return true;
}

bool TestTagAddFindUpdateExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000001"));
    linkedNotebook.setUpdateSequenceNumber(1);
    linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));
    linkedNotebook.setSharedNotebookGlobalId(QStringLiteral("Linked notebook shared notebook global id"));
    linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));
    linkedNotebook.setNoteStoreUrl(QStringLiteral("Linked notebook note store url"));
    linkedNotebook.setWebApiUrlPrefix(QStringLiteral("Linked notebook web api url prefix"));
    linkedNotebook.setStack(QStringLiteral("Linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setLinkedNotebookGuid(linkedNotebook.guid());
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    if (!tag.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid Tag: ") << tag << QStringLiteral(", error: ") << errorDescription);
        return false;
    }

    // ========== Check Add + Find ==========
    res = localStorageManager.addTag(tag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const QString localTagGuid = tag.localUid();
    Tag foundTag;
    foundTag.setLocalUid(localTagGuid);
    if (tag.hasLinkedNotebookGuid()) {
        foundTag.setLinkedNotebookGuid(tag.linkedNotebookGuid());
    }

    res = localStorageManager.findTag(foundTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (tag != foundTag) {
        errorDescription = QStringLiteral("Added and found tags in local storage tags don't match");
        QNWARNING(errorDescription << QStringLiteral(": Tag added to LocalStorageManager: ") << tag
                  << QStringLiteral("\nTag found in LocalStorageManager: ") << foundTag);
        return false;
    }

    // ========== Check Find by name ==========
    Tag foundByNameTag;
    foundByNameTag.unsetLocalUid();
    foundByNameTag.setName(tag.name());
    if (tag.hasLinkedNotebookGuid()) {
        foundByNameTag.setLinkedNotebookGuid(tag.linkedNotebookGuid());
    }

    res = localStorageManager.findTag(foundByNameTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (tag != foundByNameTag) {
        errorDescription = QStringLiteral("Tag found by name in local storage doesn't match the original tag");
        QNWARNING(errorDescription << QStringLiteral(": Tag found by name: ") << foundByNameTag << QStringLiteral("\nOriginal tag: ") << tag);
        return false;
    }

    // ========== Check Update + Find ==========
    Tag modifiedTag(tag);
    modifiedTag.setUpdateSequenceNumber(tag.updateSequenceNumber() + 1);
    modifiedTag.setLinkedNotebookGuid(QStringLiteral(""));
    modifiedTag.setName(tag.name() + QStringLiteral("_modified"));
    modifiedTag.setFavorited(true);
    modifiedTag.unsetLocalUid();

    res = localStorageManager.updateTag(modifiedTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (!modifiedTag.hasLinkedNotebookGuid()) {
        foundTag.setLinkedNotebookGuid(QStringLiteral(""));
    }

    res = localStorageManager.findTag(foundTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    modifiedTag.setLocalUid(localTagGuid);
    if (modifiedTag != foundTag) {
        errorDescription = QStringLiteral("Updated and found tags in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": Tag updated in LocalStorageManaged: ") << modifiedTag
                  << QStringLiteral("\nTag found in LocalStorageManager: ") << foundTag);
        return false;
    }

    // ========== tagCount to return 1 ============
    int count = localStorageManager.tagCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("tagCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Add another tag referencing the first tag as its parent =========
    Tag newTag;
    newTag.setName(QStringLiteral("New tag"));
    newTag.setParentGuid(tag.guid());
    newTag.setParentLocalUid(tag.localUid());

    res = localStorageManager.addTag(newTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Tag foundNewTag;
    foundNewTag.setLocalUid(newTag.localUid());
    res = localStorageManager.findTag(foundNewTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (newTag != foundNewTag) {
        errorDescription = QStringLiteral("Second added tag and its found copy from the local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": the second tag added to LocalStorageManager: ") << newTag
                  << QStringLiteral("\nTag found in LocalStorageManager: ") << foundNewTag);
        return false;
    }

    // ========== Check Expunge + Find (failure expected) ==========
    QStringList expungedChildTagLocalUids;
    res = localStorageManager.expungeTag(modifiedTag, expungedChildTagLocalUids, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Q_UNUSED(expungedChildTagLocalUids)

    res = localStorageManager.findTag(foundTag, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found tag which should have been exounged from local storage");
        QNWARNING(errorDescription << QStringLiteral(": Tag expunged from LocalStorageManager: ") << modifiedTag
                  << QStringLiteral("\nTag found in LocalStorageManager: ") << foundTag);
        return false;
    }

    return true;
}

bool TestResourceAddFindUpdateExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalUid(notebook.localUid());

    errorMessage.clear();
    res = localStorageManager.addNote(note, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    resource.setUpdateSequenceNumber(1);
    resource.setNoteGuid(note.guid());
    resource.setDataBody(QByteArray("Fake resource data body"));
    resource.setDataSize(resource.dataBody().size());
    resource.setDataHash(QByteArray("Fake hash      1"));

    resource.setRecognitionDataBody(QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" objID=\"fc83e58282d8059be17debabb69be900\" "
                                               "engineVersion=\"5.5.22.7\" recoType=\"service\" lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                                               "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                                               "<t w=\"87\">EVER ?</t>"
                                               "<t w=\"83\">EVER NOTE</t>"
                                               "<t w=\"82\">EVERNOTE</t>"
                                               "<t w=\"71\">EVER NaTE</t>"
                                               "<t w=\"67\">EVER nine</t>"
                                               "<t w=\"67\">EVER none</t>"
                                               "<t w=\"66\">EVER not</t>"
                                               "<t w=\"62\">over NOTE</t>"
                                               "<t w=\"62\">even NOTE</t>"
                                               "<t w=\"61\">EVER nose</t>"
                                               "<t w=\"50\">EV£RNoTE</t>"
                                               "</item>"
                                               "<item x=\"1850\" y=\"1465\" w=\"14\" h=\"12\">"
                                               "<t w=\"11\">et</t>"
                                               "<t w=\"10\">TQ</t>"
                                               "</item>"
                                               "</recoIndex>"));
    resource.setRecognitionDataSize(resource.recognitionDataBody().size());
    resource.setRecognitionDataHash(QByteArray("Fake hash      2"));

    resource.setAlternateDataBody(QByteArray("Fake alternate data body"));
    resource.setAlternateDataSize(resource.alternateDataBody().size());
    resource.setAlternateDataHash(QByteArray("Fake hash      3"));

    resource.setMime(QStringLiteral("text/plain"));
    resource.setWidth(1);
    resource.setHeight(1);

    qevercloud::ResourceAttributes & resourceAttributes = resource.resourceAttributes();

    resourceAttributes.sourceURL = QStringLiteral("Fake resource source URL");
    resourceAttributes.timestamp = 1;
    resourceAttributes.latitude = 0.0;
    resourceAttributes.longitude = 0.0;
    resourceAttributes.altitude = 0.0;
    resourceAttributes.cameraMake = QStringLiteral("Fake resource camera make");
    resourceAttributes.cameraModel = QStringLiteral("Fake resource camera model");

    note.unsetLocalUid();

    if (!resource.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid Resource: ") << resource);
        return false;
    }

    // ========== Check Add + Find ==========
    res = localStorageManager.addEnResource(resource, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const QString resourceGuid = resource.guid();
    Resource foundResource;
    foundResource.setGuid(resourceGuid);
    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* withBinaryData = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (resource != foundResource) {
        errorDescription = QStringLiteral("Added and found in local storage resources don't match");
        QNWARNING(errorDescription << QStringLiteral(": Resource added to LocalStorageManager: ") << resource
                  << QStringLiteral("\nIResource found in LocalStorageManager: ") << foundResource);
        return false;
    }

    // ========== Check Update + Find ==========
    Resource modifiedResource(resource);
    modifiedResource.setUpdateSequenceNumber(resource.updateSequenceNumber() + 1);
    modifiedResource.setDataBody(resource.dataBody() + QByteArray("_modified"));
    modifiedResource.setDataSize(modifiedResource.dataBody().size());
    modifiedResource.setDataHash(QByteArray("Fake hash      3"));

    modifiedResource.setWidth(resource.width() + 1);
    modifiedResource.setHeight(resource.height() + 1);
    modifiedResource.setRecognitionDataBody(QByteArray("<recoIndex docType=\"picture\" objType=\"image\" objID=\"fc83e58282d8059be17debabb69be900\" "
                                                       "engineVersion=\"5.5.22.7\" recoType=\"service\" lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                                                       "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                                                       "<t w=\"87\">OVER ?</t>"
                                                       "<t w=\"83\">AVER NOTE</t>"
                                                       "<t w=\"82\">PVERNOTE</t>"
                                                       "<t w=\"71\">QVER NaTE</t>"
                                                       "<t w=\"67\">LVER nine</t>"
                                                       "<t w=\"67\">KVER none</t>"
                                                       "<t w=\"66\">JVER not</t>"
                                                       "<t w=\"62\">jver NOTE</t>"
                                                       "<t w=\"62\">hven NOTE</t>"
                                                       "<t w=\"61\">eVER nose</t>"
                                                       "<t w=\"50\">pV£RNoTE</t>"
                                                       "</item>"
                                                       "<item x=\"1840\" y=\"1475\" w=\"14\" h=\"12\">"
                                                       "<t w=\"11\">et</t>"
                                                       "<t w=\"10\">TQ</t>"
                                                       "</item>"
                                                       "</recoIndex>"));
    modifiedResource.setRecognitionDataSize(modifiedResource.recognitionDataBody().size());
    modifiedResource.setRecognitionDataHash(QByteArray("Fake hash      4"));
    modifiedResource.setAlternateDataBody(resource.alternateDataBody() + QByteArray("_modified"));
    modifiedResource.setAlternateDataSize(modifiedResource.alternateDataBody().size());
    modifiedResource.setAlternateDataHash(QByteArray("Fake hash      5"));

    qevercloud::ResourceAttributes & modifiedResourceAttributes = modifiedResource.resourceAttributes();

    modifiedResourceAttributes.sourceURL = QStringLiteral("Modified source URL");
    modifiedResourceAttributes.timestamp += 1;
    modifiedResourceAttributes.latitude = 2.0;
    modifiedResourceAttributes.longitude = 2.0;
    modifiedResourceAttributes.altitude = 2.0;
    modifiedResourceAttributes.cameraMake = QStringLiteral("Modified camera make");
    modifiedResourceAttributes.cameraModel = QStringLiteral("Modified camera model");

    modifiedResource.unsetLocalUid();

    res = localStorageManager.updateEnResource(modifiedResource, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* withBinaryData = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (modifiedResource != foundResource) {
        errorDescription = QStringLiteral("Updated and found in local storage resources don't match");
        QNWARNING(errorDescription << QStringLiteral(": Resource updated in LocalStorageManager: ") << modifiedResource
                  << QStringLiteral("\nIResource found in LocalStorageManager: ") << foundResource);
        return false;
    }

    // ========== Check Find without resource binary data =========
    foundResource.clear();
    foundResource.setGuid(resourceGuid);
    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* withBinaryData = */ false);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    modifiedResource.setDataBody(QByteArray());
    modifiedResource.setRecognitionDataBody(QByteArray());
    modifiedResource.setAlternateDataBody(QByteArray());

    if (modifiedResource != foundResource) {
        errorDescription = QStringLiteral("Updated and found in local storage resources without binary data don't match");
        QNWARNING(errorDescription << QStringLiteral(": Resource updated in LocalStorageManager: ") << modifiedResource
                  << QStringLiteral("\nIResource found in LocalStorageManager: ") << foundResource);
        return false;
    }


    // ========== enResourceCount to return 1 ============
    int count = localStorageManager.enResourceCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("enResourceCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Check Expunge + Find (falure expected) ==========
    res = localStorageManager.expungeEnResource(modifiedResource, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findEnResource(foundResource, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found Resource which should have been expunged from LocalStorageManager");
        QNWARNING(errorDescription << QStringLiteral(": Resource expunged from LocalStorageManager: ") << modifiedResource
                  << QStringLiteral("\nIResource found in LocalStorageManager: ") << foundResource);
        return false;
    }

    // ========== enResourceCount to return 0 ============
    count = localStorageManager.enResourceCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 0) {
        errorDescription = QStringLiteral("enResourceCount returned result different from the expected one (0): ");
        errorDescription += QString::number(count);
        return false;
    }

    return true;
}

bool TestNoteFindUpdateDeleteExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalUid(notebook.localUid());

    qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
    noteAttributes.subjectDate = 1;
    noteAttributes.latitude = 1.0;
    noteAttributes.longitude = 1.0;
    noteAttributes.altitude = 1.0;
    noteAttributes.author = QStringLiteral("author");
    noteAttributes.source = QStringLiteral("source");
    noteAttributes.sourceURL = QStringLiteral("source URL");
    noteAttributes.sourceApplication = QStringLiteral("source application");
    noteAttributes.shareDate = 2;

    qevercloud::NoteLimits & noteLimits = note.noteLimits();
    noteLimits.noteResourceCountMax = 50;
    noteLimits.uploadLimit = 268435456;
    noteLimits.resourceSizeMax = 268435456;
    noteLimits.noteSizeMax = 268435456;
    noteLimits.uploaded = 100;

    note.unsetLocalUid();

    SharedNote sharedNote;
    sharedNote.setNoteGuid(note.guid());
    sharedNote.setSharerUserId(1);
    sharedNote.setRecipientIdentityId(qint64(2));
    sharedNote.setRecipientIdentityContactName(QStringLiteral("Contact"));
    sharedNote.setRecipientIdentityContactId(QStringLiteral("Contact id"));
    sharedNote.setRecipientIdentityContactType(qevercloud::ContactType::EVERNOTE);
    sharedNote.setRecipientIdentityContactPhotoUrl(QStringLiteral("url"));
    sharedNote.setRecipientIdentityContactPhotoLastUpdated(qint64(50));
    sharedNote.setRecipientIdentityContactMessagingPermit(QByteArray("aaa"));
    sharedNote.setRecipientIdentityContactMessagingPermitExpires(qint64(1));
    sharedNote.setRecipientIdentityUserId(3);
    sharedNote.setRecipientIdentityDeactivated(false);
    sharedNote.setRecipientIdentitySameBusiness(true);
    sharedNote.setRecipientIdentityBlocked(true);
    sharedNote.setRecipientIdentityUserConnected(true);
    sharedNote.setRecipientIdentityEventId(qint64(5));
    sharedNote.setPrivilegeLevel(qevercloud::SharedNotePrivilegeLevel::FULL_ACCESS);
    sharedNote.setCreationTimestamp(6);
    sharedNote.setModificationTimestamp(7);
    sharedNote.setAssignmentTimestamp(8);
    note.addSharedNote(sharedNote);

    errorMessage.clear();
    res = localStorageManager.addNote(note, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();
    res = localStorageManager.addTag(tag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    note.addTagGuid(tag.guid());
    note.addTagLocalUid(tag.localUid());

    errorMessage.clear();
    res = localStorageManager.updateNote(note, /* updateResources = */ false, /* updateTags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    resource.setUpdateSequenceNumber(1);
    resource.setNoteGuid(note.guid());
    resource.setDataBody(QByteArray("Fake resource data body"));
    resource.setDataSize(resource.dataBody().size());
    resource.setDataHash(QByteArray("Fake hash      1"));
    resource.setMime(QStringLiteral("text/plain"));
    resource.setWidth(1);
    resource.setHeight(1);
    resource.setRecognitionDataBody(QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" objID=\"fc83e58282d8059be17debabb69be900\" "
                                               "engineVersion=\"5.5.22.7\" recoType=\"service\" lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                                               "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                                               "<t w=\"87\">EVER ?</t>"
                                               "<t w=\"83\">EVER NOTE</t>"
                                               "<t w=\"82\">EVERNOTE</t>"
                                               "<t w=\"71\">EVER NaTE</t>"
                                               "<t w=\"67\">EVER nine</t>"
                                               "<t w=\"67\">EVER none</t>"
                                               "<t w=\"66\">EVER not</t>"
                                               "<t w=\"62\">over NOTE</t>"
                                               "<t w=\"62\">even NOTE</t>"
                                               "<t w=\"61\">EVER nose</t>"
                                               "<t w=\"50\">EV£RNoTE</t>"
                                               "</item>"
                                               "<item x=\"1850\" y=\"1465\" w=\"14\" h=\"12\">"
                                               "<t w=\"11\">et</t>"
                                               "<t w=\"10\">TQ</t>"
                                               "</item>"
                                               "</recoIndex>"));
    resource.setRecognitionDataSize(resource.recognitionDataBody().size());
    resource.setRecognitionDataHash(QByteArray("Fake hash      2"));

    qevercloud::ResourceAttributes & resourceAttributes = resource.resourceAttributes();

    resourceAttributes.sourceURL = QStringLiteral("Fake resource source URL");
    resourceAttributes.timestamp = 1;
    resourceAttributes.latitude = 0.0;
    resourceAttributes.longitude = 0.0;
    resourceAttributes.altitude = 0.0;
    resourceAttributes.cameraMake = QStringLiteral("Fake resource camera make");
    resourceAttributes.cameraModel = QStringLiteral("Fake resource camera model");

    note.addResource(resource);

    errorMessage.clear();
    res = localStorageManager.updateNote(note, /* update resources = */ true,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // ========== Check Find ==========
    const QString initialResourceGuid = QStringLiteral("00000000-0000-0000-c000-000000000049");
    Resource foundResource;
    foundResource.setGuid(initialResourceGuid);
    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* withBinaryData = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const QString noteGuid = note.guid();
    const bool withResourceMetadata = true;
    const bool withResourceBinaryData = true;
    Note foundNote;
    foundNote.setGuid(noteGuid);
    res = localStorageManager.findNote(foundNote, errorMessage, withResourceMetadata, withResourceBinaryData);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // NOTE: foundNote was searched by guid and might have another local uid is the original note
    // doesn't have one. So use this workaround to ensure the comparison is good for everything
    // without local uid
    if (note.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    if (note != foundNote) {
        errorDescription = QStringLiteral("Added and found notes in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": Note added to LocalStorageManager: ") << note
                  << QStringLiteral("\nNote found in LocalStorageManager: ") << foundNote);
        return false;
    }

    // ========== Check Update + Find ==========
    Note modifiedNote(note);
    modifiedNote.setUpdateSequenceNumber(note.updateSequenceNumber() + 1);
    modifiedNote.setTitle(note.title() + QStringLiteral("_modified"));
    modifiedNote.setCreationTimestamp(note.creationTimestamp() + 1);
    modifiedNote.setModificationTimestamp(note.modificationTimestamp() + 1);
    modifiedNote.setFavorited(true);

    qevercloud::NoteAttributes & modifiedNoteAttributes = modifiedNote.noteAttributes();

    modifiedNoteAttributes.subjectDate = 2;
    modifiedNoteAttributes.latitude = 2.0;
    modifiedNoteAttributes.longitude = 2.0;
    modifiedNoteAttributes.altitude = 2.0;
    modifiedNoteAttributes.author = QStringLiteral("modified author");
    modifiedNoteAttributes.source = QStringLiteral("modified source");
    modifiedNoteAttributes.sourceURL = QStringLiteral("modified source URL");
    modifiedNoteAttributes.sourceApplication = QStringLiteral("modified source application");
    modifiedNoteAttributes.shareDate = 2;

    Tag newTag;
    newTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000050"));
    newTag.setUpdateSequenceNumber(1);
    newTag.setName(QStringLiteral("Fake new tag name"));

    res = localStorageManager.addTag(newTag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Can't add new tag to local storage manager: ") << errorDescription);
        return false;
    }

    modifiedNote.addTagGuid(newTag.guid());
    modifiedNote.addTagLocalUid(newTag.localUid());

    Resource newResource;
    newResource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000051"));
    newResource.setUpdateSequenceNumber(2);
    newResource.setNoteGuid(note.guid());
    newResource.setDataBody(QByteArray("Fake new resource data body"));
    newResource.setDataSize(newResource.dataBody().size());
    newResource.setDataHash(QByteArray("Fake hash      3"));
    newResource.setMime(QStringLiteral("text/plain"));
    newResource.setWidth(2);
    newResource.setHeight(2);
    newResource.setRecognitionDataBody(QByteArray("<recoIndex docType=\"picture\" objType=\"image\" objID=\"fc83e58282d8059be17debabb69be900\" "
                                                  "engineVersion=\"5.5.22.7\" recoType=\"service\" lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                                                  "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                                                  "<t w=\"87\">OVER ?</t>"
                                                  "<t w=\"83\">AVER NOTE</t>"
                                                  "<t w=\"82\">PVERNOTE</t>"
                                                  "<t w=\"71\">QVER NaTE</t>"
                                                  "<t w=\"67\">LVER nine</t>"
                                                  "<t w=\"67\">KVER none</t>"
                                                  "<t w=\"66\">JVER not</t>"
                                                  "<t w=\"62\">jver NOTE</t>"
                                                  "<t w=\"62\">hven NOTE</t>"
                                                  "<t w=\"61\">eVER nose</t>"
                                                  "<t w=\"50\">pV£RNoTE</t>"
                                                  "</item>"
                                                  "<item x=\"1840\" y=\"1475\" w=\"14\" h=\"12\">"
                                                  "<t w=\"11\">et</t>"
                                                  "<t w=\"10\">TQ</t>"
                                                  "</item>"
                                                  "</recoIndex>"));
    newResource.setRecognitionDataSize(newResource.recognitionDataBody().size());
    newResource.setRecognitionDataHash(QByteArray("Fake hash      4"));

    qevercloud::ResourceAttributes & newResourceAttributes = newResource.resourceAttributes();

    newResourceAttributes.sourceURL = QStringLiteral("Fake resource source URL");
    newResourceAttributes.timestamp = 1;
    newResourceAttributes.latitude = 0.0;
    newResourceAttributes.longitude = 0.0;
    newResourceAttributes.altitude = 0.0;
    newResourceAttributes.cameraMake = QStringLiteral("Fake resource camera make");
    newResourceAttributes.cameraModel = QStringLiteral("Fake resource camera model");

    newResourceAttributes.applicationData = qevercloud::LazyMap();

    newResourceAttributes.applicationData->keysOnly = QSet<QString>();
    auto & keysOnly = newResourceAttributes.applicationData->keysOnly.ref();
    keysOnly.reserve(1);
    keysOnly.insert(QStringLiteral("key 1"));

    newResourceAttributes.applicationData->fullMap = QMap<QString, QString>();
    auto & fullMap = newResourceAttributes.applicationData->fullMap.ref();
    fullMap[QStringLiteral("key 1 map")] = QStringLiteral("value 1");

    modifiedNote.addResource(newResource);

    modifiedNote.unsetLocalUid();
    modifiedNote.setNotebookLocalUid(notebook.localUid());

    res = localStorageManager.updateNote(modifiedNote, /* update resources = */ true,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundResource = Resource();
    foundResource.setGuid(newResource.guid());
    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* withBinaryData = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundResource.setNoteLocalUid(QString());
    if (foundResource != newResource)
    {
        errorDescription = QStringLiteral("Something is wrong with the new resource "
                                          "which should have been added to local storage "
                                          "along with note update: it is not equal to original resource");
        QNWARNING(errorDescription << QStringLiteral(": original resource: ") << newResource
                  << QStringLiteral("\nfound resource: ") << foundResource);
        return false;
    }

    res = localStorageManager.findNote(foundNote, errorMessage,
                                       /* with resource metadata = */ true,
                                       /* with resource binary data = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // NOTE: foundNote was searched by guid and might have another local uid is the original note
    // doesn't have one. So use this workaround to ensure the comparison is good for everything
    // without local uid
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    if (modifiedNote != foundNote) {
        errorDescription = QStringLiteral("Updated and found in local storage notes don't match");
        QNWARNING(errorDescription << QStringLiteral(": Note updated in LocalStorageManager: ") << modifiedNote
                  << QStringLiteral("\nNote found in LocalStorageManager: ") << foundNote);
        return false;
    }

    Note newNote;
    newNote.setNotebookGuid(notebook.guid());
    newNote.setTitle(QStringLiteral("New note"));
    newNote.addTagGuid(tag.guid());
    newNote.addTagLocalUid(tag.localUid());

    res = localStorageManager.addNote(newNote, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // ========== noteCount to return 2 ============
    int count = localStorageManager.noteCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 2) {
        errorDescription = QStringLiteral("noteCount returned result different from the expected one (2): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== noteCountPerNotebook to return 2 ===========
    count = localStorageManager.noteCountPerNotebook(notebook, errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 2) {
        errorDescription = QStringLiteral("noteCountPerNotebook returned result different from the expected one (2): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== noteCountPerTag to return 1 for new tag ==========
    count = localStorageManager.noteCountPerTag(newTag, errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("noteCountPerTag returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== noteCountPerTag to return 2 for old tag ==========
    count = localStorageManager.noteCountPerTag(tag, errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 2) {
        errorDescription = QStringLiteral("noteCountPerTag returned result different from the expected one (2): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Note count per all tags to return 2 and 1 for first and second tags ============
    QHash<QString, int> noteCountsPerTagLocalUid;
    res = localStorageManager.noteCountsPerAllTags(noteCountsPerTagLocalUid, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (noteCountsPerTagLocalUid.size() != 2) {
        errorDescription = QStringLiteral("Unexpected amount of tag local uids within the hash of note counts by tag local uid: expected 2, got ");
        errorDescription += QString::number(noteCountsPerTagLocalUid.size());
        return false;
    }

    auto firstTagNoteCountIt = noteCountsPerTagLocalUid.find(tag.localUid());
    if (Q_UNLIKELY(firstTagNoteCountIt == noteCountsPerTagLocalUid.end())) {
        errorDescription = QStringLiteral("Can't find the note count for first tag's local uid");
        return false;
    }

    if (firstTagNoteCountIt.value() != 2) {
        errorDescription = QStringLiteral("Unexpected note count for the first tag: expected 2, got ");
        errorDescription += QString::number(firstTagNoteCountIt.value());
        return false;
    }

    auto secondTagNoteCountIt = noteCountsPerTagLocalUid.find(newTag.localUid());
    if (Q_UNLIKELY(secondTagNoteCountIt == noteCountsPerTagLocalUid.end())) {
        errorDescription = QStringLiteral("Can;t find the note count for second tag's local uid");
        return false;
    }

    if (secondTagNoteCountIt.value() != 1) {
        errorDescription = QStringLiteral("Unexpected note count for the second tag: expected 1, got ");
        errorDescription += QString::number(secondTagNoteCountIt.value());
        return false;
    }

    // ========== Check Delete + Find and check deleted flag ============
    modifiedNote.setActive(false);
    modifiedNote.setDeletionTimestamp(1);
    foundNote.setActive(true);
    res = localStorageManager.updateNote(modifiedNote, /* update resources = */ false,
                                         /* update tags = */ false, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findNote(foundNote, errorMessage,
                                       /* with resource metadata = */ true,
                                       /* with resource binary data = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (!foundNote.hasActive() || foundNote.active()) {
        errorDescription = QStringLiteral("Note which should have been marked non-active "
                                          "is not marked so after LocalStorageManager::FindNote");
        QNWARNING(errorDescription << QStringLiteral(": Note found in LocalStorageManager: ") << foundNote);
        return false;
    }

    // ========== noteCount to return 1 ============
    count = localStorageManager.noteCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("noteCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Check Expunge + Find (failure expected) ==========
    res = localStorageManager.expungeNote(modifiedNote, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findNote(foundNote, errorMessage,
                                       /* with resource metadata = */ true,
                                       /* with resource binary data = */ true);
    if (res) {
        errorDescription = QStringLiteral("Error: found Note which should have been expunged "
                                          "from LocalStorageManager");
        QNWARNING(errorDescription << QStringLiteral(": Note expunged from LocalStorageManager: ") << modifiedNote
                  << QStringLiteral("\nNote found in LocalStorageManager: ") << foundNote);
        return false;
    }

    // ========== Try to find resource belonging to expunged note (failure expected) ==========
    foundResource = Resource();
    foundResource.setGuid(newResource.guid());
    res = localStorageManager.findEnResource(foundResource, errorMessage,
                                             /* with binary data = */ true);
    if (res) {
        errorDescription = QStringLiteral("Error: found Resource which should have been expunged "
                                          "from LocalStorageManager along with Note owning it");
        QNWARNING(errorDescription << QStringLiteral(": Note expunged from LocalStorageManager: ") << modifiedNote
                  << QStringLiteral("\nResource found in LocalStorageManager: ") << foundResource);
        return false;
    }

    return true;
}

bool TestNotebookFindUpdateDeleteExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000001"));
    linkedNotebook.setUpdateSequenceNumber(1);
    linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));
    linkedNotebook.setSharedNotebookGlobalId(QStringLiteral("Linked notebook shared notebook global id"));
    linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));
    linkedNotebook.setNoteStoreUrl(QStringLiteral("Linked notebook note store url"));
    linkedNotebook.setWebApiUrlPrefix(QStringLiteral("Linked notebook web api url prefix"));
    linkedNotebook.setStack(QStringLiteral("Linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setLinkedNotebookGuid(linkedNotebook.guid());
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);
    notebook.setDefaultNotebook(true);
    notebook.setLastUsed(false);
    notebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    notebook.setPublishingOrder(1);
    notebook.setPublishingAscending(true);
    notebook.setPublishingPublicDescription(QStringLiteral("Fake public description"));
    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));
    notebook.setBusinessNotebookDescription(QStringLiteral("Fake business notebook description"));
    notebook.setBusinessNotebookPrivilegeLevel(1);
    notebook.setBusinessNotebookRecommended(true);

    // NotebookRestrictions
    notebook.setCanReadNotes(true);
    notebook.setCanCreateNotes(true);
    notebook.setCanUpdateNotes(true);
    notebook.setCanExpungeNotes(false);
    notebook.setCanShareNotes(true);
    notebook.setCanEmailNotes(true);
    notebook.setCanSendMessageToRecipients(true);
    notebook.setCanUpdateNotebook(true);
    notebook.setCanExpungeNotebook(false);
    notebook.setCanSetDefaultNotebook(true);
    notebook.setCanSetNotebookStack(true);
    notebook.setCanPublishToPublic(true);
    notebook.setCanPublishToBusinessLibrary(false);
    notebook.setCanCreateTags(true);
    notebook.setCanUpdateTags(true);
    notebook.setCanExpungeTags(false);
    notebook.setCanSetParentTag(true);
    notebook.setCanCreateSharedNotebooks(true);
    notebook.setCanCreateSharedNotebooks(true);
    notebook.setCanUpdateNotebook(true);
    notebook.setUpdateWhichSharedNotebookRestrictions(1);
    notebook.setExpungeWhichSharedNotebookRestrictions(1);

    SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(1);
    sharedNotebook.setNotebookGuid(notebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setCreationTimestamp(1);
    sharedNotebook.setModificationTimestamp(1);
    sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id"));
    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilegeLevel(1);
    sharedNotebook.setReminderNotifyEmail(true);
    sharedNotebook.setReminderNotifyApp(false);

    notebook.addSharedNotebook(sharedNotebook);

    ErrorString errorMessage;
    bool res = localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    errorMessage.clear();
    res = localStorageManager.addNotebook(notebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalUid(notebook.localUid());

    errorMessage.clear();
    res = localStorageManager.addNote(note, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();
    res = localStorageManager.addTag(tag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    note.addTagGuid(tag.guid());
    note.addTagLocalUid(tag.localUid());

    errorMessage.clear();
    res = localStorageManager.updateNote(note, /* updateResources = */ false,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (!notebook.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid Notebook: ") << notebook);
        return false;
    }

    // =========== Check Find ============
    const QString initialNoteGuid = QStringLiteral("00000000-0000-0000-c000-000000000049");
    Note foundNote;
    foundNote.setGuid(initialNoteGuid);
    res = localStorageManager.findNote(foundNote, errorMessage,
                                       /* withResourceBinaryData = */ true);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());
    if (notebook.hasLinkedNotebookGuid()) {
        foundNotebook.setLinkedNotebookGuid(notebook.linkedNotebookGuid());
    }

    res = localStorageManager.findNotebook(foundNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (notebook != foundNotebook) {
        errorDescription = QStringLiteral("Added and found notebooks in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": Notebook added to LocalStorageManager: ") << notebook
                  << QStringLiteral("\nNotebook found in LocalStorageManager: ") << foundNotebook);
        return false;
    }

    // ========== Check Find by name ===========
    Notebook foundByNameNotebook;
    foundByNameNotebook.unsetLocalUid();
    foundByNameNotebook.setName(notebook.name());
    if (notebook.hasLinkedNotebookGuid()) {
        foundByNameNotebook.setLinkedNotebookGuid(notebook.linkedNotebookGuid());
    }

    res = localStorageManager.findNotebook(foundByNameNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (notebook != foundByNameNotebook) {
        errorDescription = QStringLiteral("Notebook found by name in local storage doesn't match the original notebook");
        QNWARNING(errorDescription << QStringLiteral(": Notebook found by name: ") << foundByNameNotebook
                  << QStringLiteral("\nOriginal notebook: ") << notebook);
        return false;
    }

    if (notebook.hasLinkedNotebookGuid())
    {
        // ========== Check Find by linked notebook guid ===========
        Notebook foundByLinkedNotebookGuidNotebook;
        foundByLinkedNotebookGuidNotebook.unsetLocalUid();
        foundByLinkedNotebookGuidNotebook.setLinkedNotebookGuid(notebook.linkedNotebookGuid());

        res = localStorageManager.findNotebook(foundByLinkedNotebookGuidNotebook, errorMessage);
        if (!res) {
            errorDescription = errorMessage.nonLocalizedString();
            return false;
        }

        if (notebook != foundByLinkedNotebookGuidNotebook) {
            errorDescription = QStringLiteral("Notebook found by linked notebook guid in local storage doesn't match the original notebook");
            QNWARNING(errorDescription << QStringLiteral(": Notebook found by linked notebook guid: ") << foundByLinkedNotebookGuidNotebook
                      << QStringLiteral("\nOriginal notebook: ") << notebook);
            return false;
        }
    }

    // ========== Check FindDefaultNotebook =========
    Notebook defaultNotebook;
    res = localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // ========== Check FindLastUsedNotebook (failure expected) ==========
    Notebook lastUsedNotebook;
    res = localStorageManager.findLastUsedNotebook(lastUsedNotebook, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Found some last used notebook which shouldn't have been found");
        QNWARNING(errorDescription << QStringLiteral(": ") << lastUsedNotebook);
        return false;
    }

    // ========== Check FindDefaultOrLastUsedNotebook ===========
    Notebook defaultOrLastUsedNotebook;
    res = localStorageManager.findDefaultOrLastUsedNotebook(defaultOrLastUsedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (defaultOrLastUsedNotebook != defaultNotebook) {
        errorDescription = QStringLiteral("Found defaultOrLastUsed notebook which should be the same "
                                          "as default notebook right now but it is not");
        QNWARNING(errorDescription << QStringLiteral(". Default notebook: ") << defaultNotebook
                  << QStringLiteral(", defaultOrLastUsedNotebook: ") << defaultOrLastUsedNotebook);
        return false;
    }

    // ========== Check Update + Find ==========
    Notebook modifiedNotebook(notebook);
    modifiedNotebook.setUpdateSequenceNumber(notebook.updateSequenceNumber() + 1);
    modifiedNotebook.setLinkedNotebookGuid(QStringLiteral(""));
    modifiedNotebook.setName(notebook.name() + QStringLiteral("_modified"));
    modifiedNotebook.setDefaultNotebook(false);
    modifiedNotebook.setLastUsed(true);
    modifiedNotebook.setModificationTimestamp(notebook.modificationTimestamp() + 1);
    modifiedNotebook.setPublishingUri(notebook.publishingUri() + QStringLiteral("_modified"));
    modifiedNotebook.setPublishingAscending(!notebook.isPublishingAscending());
    modifiedNotebook.setPublishingPublicDescription(notebook.publishingPublicDescription() + QStringLiteral("_modified"));
    modifiedNotebook.setStack(notebook.stack() + QStringLiteral("_modified"));
    modifiedNotebook.setBusinessNotebookDescription(notebook.businessNotebookDescription() + QStringLiteral("_modified"));
    modifiedNotebook.setBusinessNotebookRecommended(!notebook.isBusinessNotebookRecommended());
    modifiedNotebook.setCanExpungeNotes(false);
    modifiedNotebook.setCanEmailNotes(false);
    modifiedNotebook.setCanPublishToPublic(false);
    modifiedNotebook.setFavorited(true);

    res = localStorageManager.updateNotebook(modifiedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundNotebook = Notebook();
    foundNotebook.setGuid(modifiedNotebook.guid());
    if (modifiedNotebook.hasLinkedNotebookGuid()) {
        foundNotebook.setLinkedNotebookGuid(modifiedNotebook.linkedNotebookGuid());
    }

    res = localStorageManager.findNotebook(foundNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (modifiedNotebook != foundNotebook) {
        errorDescription = QStringLiteral("Updated and found notebooks in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": Notebook updated in LocalStorageManager: ") << modifiedNotebook
                  << QStringLiteral("\nNotebook found in LocalStorageManager: ") << foundNotebook);
        return false;
    }

    // ========== Check FindDefaultNotebook (failure expected) =========
    defaultNotebook = Notebook();
    res = localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Found some default notebook which shouldn't have been found");
        QNWARNING(errorDescription << QStringLiteral(": ") << defaultNotebook);
        return false;
    }

    // ========== Check FindLastUsedNotebook  ==========
    lastUsedNotebook = Notebook();
    res = localStorageManager.findLastUsedNotebook(lastUsedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // ========== Check FindDefaultOrLastUsedNotebook ===========
    defaultOrLastUsedNotebook = Notebook();
    res = localStorageManager.findDefaultOrLastUsedNotebook(defaultOrLastUsedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (defaultOrLastUsedNotebook != lastUsedNotebook) {
        errorDescription = QStringLiteral("Found defaultOrLastUsed notebook which should be the same "
                                          "as last used notebook right now but it is not");
        QNWARNING(errorDescription << QStringLiteral(". Last used notebook: ") << lastUsedNotebook
                  << QStringLiteral(", defaultOrLastUsedNotebook: ") << defaultOrLastUsedNotebook);
        return false;
    }

    // ========== Check notebookCount to return 1 ============
    int count = localStorageManager.notebookCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("notebookCount returned result different from the expected one (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Check Expunge + Find (failure expected) ==========
    res = localStorageManager.expungeNotebook(modifiedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    res = localStorageManager.findNotebook(foundNotebook, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found Notebook which should have been expunged from LocalStorageManager");
        QNWARNING(errorDescription << QStringLiteral(": Notebook expunged from LocalStorageManager: ") << modifiedNotebook
                  << QStringLiteral("\nNotebook found in LocalStorageManager: ") << foundNotebook);
        return false;
    }

    // ========== Check notebookCount to return 0 ============
    count = localStorageManager.notebookCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 0) {
        errorDescription = QStringLiteral("notebookCount returned result different from the expected one (0): ");
        errorDescription += QString::number(count);
        return false;
    }

    return true;
}

bool TestUserAddFindUpdateDeleteExpungeInLocalStorage(QString & errorDescription)
{
    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    User user;
    user.setId(1);
    user.setUsername(QStringLiteral("fake_user_username"));
    user.setEmail(QStringLiteral("fake_user _mail"));
    user.setName(QStringLiteral("fake_user_name"));
    user.setTimezone(QStringLiteral("fake_user_timezone"));
    user.setPrivilegeLevel(1);
    user.setCreationTimestamp(2);
    user.setModificationTimestamp(3);
    user.setActive(true);

    qevercloud::UserAttributes userAttributes;
    userAttributes.defaultLocationName = QStringLiteral("fake_default_location_name");
    userAttributes.defaultLatitude = 1.0;
    userAttributes.defaultLongitude = 2.0;
    userAttributes.preactivation = false;
    QList<QString> viewedPromotions;
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 1"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 2"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 3"));
    userAttributes.viewedPromotions = viewedPromotions;
    userAttributes.incomingEmailAddress = QStringLiteral("fake_incoming_email_address");
    QList<QString> recentEmailAddresses;
    recentEmailAddresses.push_back(QStringLiteral("recent_email_address_1"));
    recentEmailAddresses.push_back(QStringLiteral("recent_email_address_2"));
    userAttributes.recentMailedAddresses = recentEmailAddresses;
    userAttributes.comments = QStringLiteral("Fake comments");
    userAttributes.dateAgreedToTermsOfService = 1;
    userAttributes.maxReferrals = 3;
    userAttributes.refererCode = QStringLiteral("fake_referer_code");
    userAttributes.sentEmailDate = 5;
    userAttributes.sentEmailCount = 4;
    userAttributes.dailyEmailLimit = 2;
    userAttributes.emailOptOutDate = 6;
    userAttributes.partnerEmailOptInDate = 7;
    userAttributes.preferredLanguage = QStringLiteral("ru");
    userAttributes.preferredCountry = QStringLiteral("Russia");
    userAttributes.clipFullPage = true;
    userAttributes.twitterUserName = QStringLiteral("fake_twitter_username");
    userAttributes.twitterId = QStringLiteral("fake_twitter_id");
    userAttributes.groupName = QStringLiteral("fake_group_name");
    userAttributes.recognitionLanguage = QStringLiteral("ru");
    userAttributes.referralProof = QStringLiteral("I_have_no_idea_what_this_means");
    userAttributes.educationalDiscount = false;
    userAttributes.businessAddress = QStringLiteral("fake_business_address");
    userAttributes.hideSponsorBilling = true;
    userAttributes.useEmailAutoFiling = true;
    userAttributes.reminderEmailConfig = qevercloud::ReminderEmailConfig::DO_NOT_SEND;

    user.setUserAttributes(std::move(userAttributes));

    qevercloud::BusinessUserInfo businessUserInfo;
    businessUserInfo.businessId = 1;
    businessUserInfo.businessName = QStringLiteral("Fake business name");
    businessUserInfo.role = qevercloud::BusinessUserRole::NORMAL;
    businessUserInfo.email = QStringLiteral("Fake business email");

    user.setBusinessUserInfo(std::move(businessUserInfo));

    qevercloud::Accounting accounting;
    accounting.uploadLimitEnd = 9;
    accounting.uploadLimitNextMonth = 1200;
    accounting.premiumServiceStatus = qevercloud::PremiumOrderStatus::PENDING;
    accounting.premiumOrderNumber = QStringLiteral("Fake premium order number");
    accounting.premiumCommerceService = QStringLiteral("Fake premium commerce service");
    accounting.premiumServiceStart = 8;
    accounting.premiumServiceSKU = QStringLiteral("Fake code associated with the purchase");
    accounting.lastSuccessfulCharge = 7;
    accounting.lastFailedCharge = 5;
    accounting.lastFailedChargeReason = QStringLiteral("No money, no honey");
    accounting.nextPaymentDue = 12;
    accounting.premiumLockUntil = 11;
    accounting.updated = 10;
    accounting.premiumSubscriptionNumber = QStringLiteral("Fake premium subscription number");
    accounting.lastRequestedCharge = 9;
    accounting.currency = QStringLiteral("USD");
    accounting.unitPrice = 100;
    accounting.unitDiscount = 2;
    accounting.nextChargeDate = 12;

    user.setAccounting(std::move(accounting));

    qevercloud::AccountLimits accountLimits;
    accountLimits.userNotebookCountMax = 10;
    accountLimits.uploadLimit = 2048;
    accountLimits.noteResourceCountMax = 10;
    accountLimits.userSavedSearchesMax = 100;
    accountLimits.noteSizeMax = 4096;
    accountLimits.userMailLimitDaily = 20;
    accountLimits.noteTagCountMax = 20;
    accountLimits.resourceSizeMax = 4096;
    accountLimits.userTagCountMax = 200;

    user.setAccountLimits(std::move(accountLimits));

    ErrorString errorMessage;

    if (!user.checkParameters(errorMessage)) {
        errorDescription = errorMessage.nonLocalizedString();
        QNWARNING(QStringLiteral("Found invalid User: ") << user);
        return false;
    }

    // ========== Check Add + Find ==========
    bool res = localStorageManager.addUser(user, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    const qint32 initialUserId = user.id();
    User foundUser;
    foundUser.setId(initialUserId);
    res = localStorageManager.findUser(foundUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (user != foundUser) {
        errorDescription = QStringLiteral("Added and found users in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": User added to LocalStorageManager: ") << user
                  << QStringLiteral("\nIUser found in LocalStorageManager: ") << foundUser);
        return false;
    }

    // ========== Check Update + Find ==========
    User modifiedUser;
    modifiedUser.setId(user.id());
    modifiedUser.setUsername(user.username() + QStringLiteral("_modified"));
    modifiedUser.setEmail(user.email() + QStringLiteral("_modified"));
    modifiedUser.setName(user.name() + QStringLiteral("_modified"));
    modifiedUser.setTimezone(user.timezone() + QStringLiteral("_modified"));
    modifiedUser.setPrivilegeLevel(user.privilegeLevel());
    modifiedUser.setCreationTimestamp(user.creationTimestamp());
    modifiedUser.setModificationTimestamp(user.modificationTimestamp() + 1);
    modifiedUser.setActive(true);

    qevercloud::UserAttributes modifiedUserAttributes;
    modifiedUserAttributes = user.userAttributes();
    modifiedUserAttributes.defaultLocationName->append(QStringLiteral("_modified"));
    modifiedUserAttributes.comments->append(QStringLiteral("_modified"));
    modifiedUserAttributes.preferredCountry->append(QStringLiteral("_modified"));
    modifiedUserAttributes.businessAddress->append(QStringLiteral("_modified"));

    modifiedUser.setUserAttributes(std::move(modifiedUserAttributes));

    qevercloud::BusinessUserInfo modifiedBusinessUserInfo;
    modifiedBusinessUserInfo = user.businessUserInfo();
    modifiedBusinessUserInfo.businessName->append(QStringLiteral("_modified"));
    modifiedBusinessUserInfo.email->append(QStringLiteral("_modified"));

    modifiedUser.setBusinessUserInfo(std::move(modifiedBusinessUserInfo));

    qevercloud::Accounting modifiedAccounting;
    modifiedAccounting = user.accounting();
    modifiedAccounting.premiumOrderNumber->append(QStringLiteral("_modified"));
    modifiedAccounting.premiumSubscriptionNumber->append(QStringLiteral("_modified"));
    modifiedAccounting.updated += 1;

    modifiedUser.setAccounting(std::move(modifiedAccounting));

    qevercloud::AccountLimits modifiedAccountLimits;
    modifiedAccountLimits = user.accountLimits();
    modifiedAccountLimits.noteTagCountMax = 2;
    modifiedAccountLimits.userLinkedNotebookMax = 2;
    modifiedAccountLimits.userNotebookCountMax = 2;

    modifiedUser.setAccountLimits(std::move(modifiedAccountLimits));

    res = localStorageManager.updateUser(modifiedUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundUser.clear();
    foundUser.setId(modifiedUser.id());
    res = localStorageManager.findUser(foundUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (modifiedUser != foundUser) {
        errorDescription = QStringLiteral("Updated and found users in local storage don't match");
        QNWARNING(errorDescription << QStringLiteral(": User updated in LocalStorageManager: ") << modifiedUser
                  << QStringLiteral("\nIUser found in LocalStorageManager: ") << foundUser);
        return false;
    }

    // ========== Check userCount to return 1 ===========
    int count = localStorageManager.userCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 1) {
        errorDescription = QStringLiteral("userCount returned value different from expected (1): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Check Delete + Find ==========
    modifiedUser.setDeletionTimestamp(5);

    res = localStorageManager.deleteUser(modifiedUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundUser.clear();
    foundUser.setId(modifiedUser.id());
    res = localStorageManager.findUser(foundUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (modifiedUser != foundUser) {
        errorDescription = QStringLiteral("Deleted and found users in local storage manager don't match");
        QNWARNING(errorDescription << QStringLiteral(": User marked deleted in LocalStorageManager: ") << modifiedUser
                  << QStringLiteral("\nIUser found in LocalStorageManager: ") << foundUser);
        return false;
    }

    // ========== Check userCount to return 0 (as it doesn't account for deleted resources) ===========
    count = localStorageManager.userCount(errorMessage);
    if (count < 0) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }
    else if (count != 0) {
        errorDescription = QStringLiteral("userCount returned value different from expected (0): ");
        errorDescription += QString::number(count);
        return false;
    }

    // ========== Check Expunge + Find (failure expected) ==========
    res = localStorageManager.expungeUser(modifiedUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    foundUser.clear();
    foundUser.setId(modifiedUser.id());
    res = localStorageManager.findUser(foundUser, errorMessage);
    if (res) {
        errorDescription = QStringLiteral("Error: found User which should have been expunged from LocalStorageManager");
        QNWARNING(errorDescription << QStringLiteral(": User expunged from LocalStorageManager: ") << modifiedUser
                  << QStringLiteral("\nIUser found in LocalStorageManager: ") << foundUser);
        return false;
    }

    return true;
}

bool TestSequentialUpdatesInLocalStorage(QString & errorDescription)
{
    // 1) ========== Create LocalStorageManager =============

    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("LocalStorageManagerSequentialUpdatesTestFakeUser"), Account::Type::Evernote, 0);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    // 2) ========== Create User ============
    User   user;
    user.setId(1);
    user.setUsername(QStringLiteral("checker"));
    user.setEmail(QStringLiteral("mail@checker.com"));
    user.setTimezone(QStringLiteral("Europe/Moscow"));
    user.setPrivilegeLevel(qevercloud::PrivilegeLevel::NORMAL);
    user.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    user.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());
    user.setActive(true);

    qevercloud::UserAttributes userAttributes;
    userAttributes.defaultLocationName = QStringLiteral("Default location");
    userAttributes.comments = QStringLiteral("My comment");
    userAttributes.preferredLanguage = QStringLiteral("English");

    userAttributes.viewedPromotions = QStringList();
    userAttributes.viewedPromotions.ref() << QStringLiteral("Promotion #1")
                                          << QStringLiteral("Promotion #2")
                                          << QStringLiteral("Promotion #3");

    userAttributes.recentMailedAddresses = QStringList();
    userAttributes.recentMailedAddresses.ref() << QStringLiteral("Recent mailed address #1")
                                               << QStringLiteral("Recent mailed address #2")
                                               << QStringLiteral("Recent mailed address #3");

    user.setUserAttributes(std::move(userAttributes));

    qevercloud::Accounting accounting;
    accounting.premiumOrderNumber = QStringLiteral("Premium order number");
    accounting.premiumSubscriptionNumber = QStringLiteral("Premium subscription number");
    accounting.updated = QDateTime::currentMSecsSinceEpoch();

    user.setAccounting(std::move(accounting));

    qevercloud::BusinessUserInfo businessUserInfo;
    businessUserInfo.businessName = QStringLiteral("Business name");
    businessUserInfo.email = QStringLiteral("Business email");

    user.setBusinessUserInfo(std::move(businessUserInfo));

    qevercloud::AccountLimits accountLimits;
    accountLimits.noteResourceCountMax = 20;
    accountLimits.userNoteCountMax = 200;
    accountLimits.userSavedSearchesMax = 100;

    user.setAccountLimits(std::move(accountLimits));

    ErrorString errorMessage;

    // 3) ============ Add user to local storage ==============
    bool res = localStorageManager.addUser(user, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 4) ============ Create new user without all the supplementary data but with the same id
    //                 and update it in local storage ===================
    User updatedUser;
    updatedUser.setId(1);
    updatedUser.setUsername(QStringLiteral("checker"));
    updatedUser.setEmail(QStringLiteral("mail@checker.com"));
    updatedUser.setPrivilegeLevel(qevercloud::PrivilegeLevel::NORMAL);
    updatedUser.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setActive(true);

    res = localStorageManager.updateUser(updatedUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 5) =========== Find this user in local storage, check whether it has user attributes,
    //                accounting, business user info and premium info (it shouldn't) =========
    User foundUser;
    foundUser.setId(1);

    res = localStorageManager.findUser(foundUser, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (foundUser.hasUserAttributes()) {
        errorDescription = QStringLiteral("Updated user found in local storage still has user attributes "
                                          "while it shouldn't have them after the update");
        QNWARNING(errorDescription << QStringLiteral(": initial user: ") << user << QStringLiteral("\nUpdated user: ")
                  << updatedUser << QStringLiteral("\nFound user: ") << foundUser);
        return false;
    }

    if (foundUser.hasAccounting()) {
        errorDescription = QStringLiteral("Updated user found in local storage still has accounting "
                                          "while it shouldn't have it after the update");
        QNWARNING(errorDescription << QStringLiteral(": initial user: ") << user << QStringLiteral("\nUpdated user: ")
                  << updatedUser << QStringLiteral("\nFound user: ") << foundUser);
        return false;
    }

    if (foundUser.hasBusinessUserInfo()) {
        errorDescription = QStringLiteral("Updated user found in local storage still has business user info "
                                          "while it shouldn't have it after the update");
        QNWARNING(errorDescription << QStringLiteral(": initial user: ") << user << QStringLiteral("\nUpdated user: ")
                  << updatedUser << QStringLiteral("\nFound user: ") << foundUser);
        return false;
    }

    if (foundUser.hasAccountLimits()) {
        errorDescription = QStringLiteral("Updated user found in local storage still has account limits "
                                          "while it shouldn't have them after the update");
        QNWARNING(errorDescription << QStringLiteral(": initial user: ") << user << QStringLiteral("\nUpdated user: ")
                  << updatedUser << QStringLiteral("\nFound user: ") << foundUser);
        return false;
    }

    // ============ 6) Create Notebook with restrictions and shared notebooks ==================
    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);
    notebook.setDefaultNotebook(true);
    notebook.setLastUsed(false);
    notebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    notebook.setPublishingOrder(1);
    notebook.setPublishingAscending(true);
    notebook.setPublishingPublicDescription(QStringLiteral("Fake public description"));
    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));
    notebook.setBusinessNotebookDescription(QStringLiteral("Fake business notebook description"));
    notebook.setBusinessNotebookPrivilegeLevel(1);
    notebook.setBusinessNotebookRecommended(true);
    // NotebookRestrictions
    notebook.setCanReadNotes(true);
    notebook.setCanCreateNotes(true);
    notebook.setCanUpdateNotes(true);
    notebook.setCanExpungeNotes(false);
    notebook.setCanShareNotes(true);
    notebook.setCanEmailNotes(false);
    notebook.setCanSendMessageToRecipients(true);
    notebook.setCanUpdateNotebook(true);
    notebook.setCanExpungeNotebook(false);
    notebook.setCanSetDefaultNotebook(true);
    notebook.setCanSetNotebookStack(false);
    notebook.setCanPublishToPublic(true);
    notebook.setCanPublishToBusinessLibrary(false);
    notebook.setCanCreateTags(true);
    notebook.setCanUpdateTags(true);
    notebook.setCanExpungeTags(false);
    notebook.setCanSetParentTag(true);
    notebook.setCanCreateSharedNotebooks(true);
    notebook.setCanCreateSharedNotebooks(true);
    notebook.setCanUpdateNotebook(true);
    notebook.setUpdateWhichSharedNotebookRestrictions(1);
    notebook.setExpungeWhichSharedNotebookRestrictions(1);

    SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(1);
    sharedNotebook.setNotebookGuid(notebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setCreationTimestamp(1);
    sharedNotebook.setModificationTimestamp(1);
    sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id"));
    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilegeLevel(1);
    sharedNotebook.setReminderNotifyEmail(true);
    sharedNotebook.setReminderNotifyApp(false);

    notebook.addSharedNotebook(sharedNotebook);

    res = localStorageManager.addNotebook(notebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 7) ============ Update notebook: remove restrictions and shared notebooks =========
    Notebook updatedNotebook;
    updatedNotebook.setLocalUid(notebook.localUid());
    updatedNotebook.setGuid(notebook.guid());
    updatedNotebook.setUpdateSequenceNumber(1);
    updatedNotebook.setName(QStringLiteral("Fake notebook name"));
    updatedNotebook.setCreationTimestamp(1);
    updatedNotebook.setModificationTimestamp(1);
    updatedNotebook.setDefaultNotebook(true);
    updatedNotebook.setLastUsed(false);
    updatedNotebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    updatedNotebook.setPublishingOrder(1);
    updatedNotebook.setPublishingAscending(true);
    updatedNotebook.setPublishingPublicDescription(QStringLiteral("Fake public description"));
    updatedNotebook.setPublished(true);
    updatedNotebook.setStack(QStringLiteral("Fake notebook stack"));
    updatedNotebook.setBusinessNotebookDescription(QStringLiteral("Fake business notebook description"));
    updatedNotebook.setBusinessNotebookPrivilegeLevel(1);
    updatedNotebook.setBusinessNotebookRecommended(true);

    res = localStorageManager.updateNotebook(updatedNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 8) ============= Find notebook, ensure it doesn't have neither restrictions nor shared notebooks

    Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());

    res = localStorageManager.findNotebook(foundNotebook, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (foundNotebook.hasSharedNotebooks()) {
        errorDescription = QStringLiteral("Updated notebook found in local storage has shared notebooks "
                                          "while it shouldn't have them");
        QNWARNING(errorDescription << QStringLiteral(", original notebook: ") << notebook
                  << QStringLiteral("\nUpdated notebook: ") << updatedNotebook << QStringLiteral("\nFound notebook: ")
                  << foundNotebook);
        return false;
    }

    if (foundNotebook.hasRestrictions()) {
        errorDescription = QStringLiteral("Updated notebook found in local storage has restrictions "
                                          "while it shouldn't have them");
        QNWARNING(errorDescription << QStringLiteral(", original notebook: ") << notebook
                  << QStringLiteral("\nUpdated notebook: ") << updatedNotebook << QStringLiteral("\nFound notebook: ")
                  << foundNotebook);
        return false;
    }

    // 9) ============== Create tag =================
    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    res = localStorageManager.addTag(tag, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 10) ============= Create note, add this tag to it along with some resource ===========
    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());

    Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000044"));
    resource.setUpdateSequenceNumber(1);
    resource.setNoteGuid(note.guid());
    resource.setDataBody(QByteArray("Fake resource data body"));
    resource.setDataSize(resource.dataBody().size());
    resource.setDataHash(QByteArray("Fake hash      1"));

    note.addResource(resource);
    note.addTagGuid(tag.guid());
    note.setNotebookLocalUid(updatedNotebook.localUid());

    res = localStorageManager.addNote(note, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 11) ============ Update note, remove tag guid and resource ============
    Note updatedNote;
    updatedNote.setLocalUid(note.localUid());
    updatedNote.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
    updatedNote.setUpdateSequenceNumber(1);
    updatedNote.setTitle(QStringLiteral("Fake note title"));
    updatedNote.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    updatedNote.setCreationTimestamp(1);
    updatedNote.setModificationTimestamp(1);
    updatedNote.setActive(true);
    updatedNote.setNotebookGuid(notebook.guid());
    updatedNote.setNotebookLocalUid(notebook.localUid());

    res = localStorageManager.updateNote(updatedNote, /* update resources = */ true,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 12) =========== Find updated note in local storage, ensure it doesn't have neither tag guids, nor resources
    Note foundNote;
    foundNote.setLocalUid(updatedNote.localUid());
    foundNote.setGuid(updatedNote.guid());

    res = localStorageManager.findNote(foundNote, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    if (foundNote.hasTagGuids()) {
        errorDescription = QStringLiteral("Updated note found in local storage has tag guids while it shouldn't have them");
        QNWARNING(errorDescription << QStringLiteral(", original note: ") << note << QStringLiteral("\nUpdated note: ")
                  << updatedNote << QStringLiteral("\nFound note: ") << foundNote);
        return false;
    }

    if (foundNote.hasResources()) {
        errorDescription = QStringLiteral("Updated note found in local storage has resources while it shouldn't have them");
        QNWARNING(errorDescription << QStringLiteral(", original note: ") << note << QStringLiteral("\nUpdated note: ")
                  << updatedNote << QStringLiteral("\nFound note: ") << foundNote);
        return false;
    }

    // 13) ============== Add resource attributes to the resource and add resource to note =============
    qevercloud::ResourceAttributes & resourceAttributes = resource.resourceAttributes();
    resourceAttributes.applicationData = qevercloud::LazyMap();
    resourceAttributes.applicationData->keysOnly = QSet<QString>();
    resourceAttributes.applicationData->fullMap = QMap<QString, QString>();

    resourceAttributes.applicationData->keysOnly.ref() << QStringLiteral("key_1") << QStringLiteral("key_2") << QStringLiteral("key_3");
    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_1")] = QStringLiteral("value_1");
    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_2")] = QStringLiteral("value_2");
    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_3")] = QStringLiteral("value_3");

    updatedNote.addResource(resource);

    res = localStorageManager.updateNote(updatedNote, /* update resources = */ true,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return res;
    }

    // 14) ================ Remove resource attributes from note's resource and update it again
    QList<Resource> resources = updatedNote.resources();
    if (resources.empty()) {
        errorDescription = QStringLiteral("Note returned empty list of resource adapters while it should have "
                                          "contained at least one entry");
        QNWARNING(errorDescription << QStringLiteral(", updated note: ") << updatedNote);
        return false;
    }

    Resource & resourceWrapper = resources[0];
    qevercloud::ResourceAttributes & underlyngResourceAttributes = resourceWrapper.resourceAttributes();
    underlyngResourceAttributes = qevercloud::ResourceAttributes();

    updatedNote.setResources(resources);

    res = localStorageManager.updateNote(updatedNote, /* update resources = */ true,
                                         /* update tags = */ true, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    // 15) ============= Find note in local storage again ===============
    res = localStorageManager.findNote(foundNote, errorMessage);
    if (!res) {
        errorDescription = errorMessage.nonLocalizedString();
        return false;
    }

    resources = foundNote.resources();
    if (resources.empty()) {
        errorDescription = QStringLiteral("Note returned empty list of resource adapters while it should have "
                                          "contained at least one entry");
        QNWARNING(errorDescription << ", found note: " << foundNote);
        return false;
    }

    Resource & foundResourceWrapper = resources[0];
    qevercloud::ResourceAttributes & foundResourceAttributes = foundResourceWrapper.resourceAttributes();
    if (foundResourceAttributes.applicationData.isSet()) {
        errorDescription = QStringLiteral("Resource from updated note has application data while it shouldn't have it");
        QNWARNING(errorDescription << QStringLiteral(", found resource: ") << foundResourceWrapper);
        return false;
    }

    return true;
}

bool TestAccountHighUsnInLocalStorage(QString & errorDescription)
{
    // 1) ========== Create LocalStorageManager =============

    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("LocalStorageManagerAccountHighUsnTestFakeUser"), Account::Type::Evernote, 0);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    ErrorString error;

    // 2) ========== Verify that account high USN is initially zero (since all tables are empty) ==========

    error.clear();
    qint32 initialUsn = localStorageManager.accountHighUsn(QString(), error);
    if (initialUsn != 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    qint32 currentUsn = initialUsn;

    // 3) ========== Create some user's own notebooks with different USNs ==========

    Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setUpdateSequenceNumber(currentUsn++);
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstNotebook.setModificationTimestamp(firstNotebook.creationTimestamp());
    firstNotebook.setDefaultNotebook(true);
    firstNotebook.setLastUsed(false);

    Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setUpdateSequenceNumber(currentUsn++);
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondNotebook.setModificationTimestamp(secondNotebook.creationTimestamp());
    secondNotebook.setDefaultNotebook(false);
    secondNotebook.setLastUsed(false);

    Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setUpdateSequenceNumber(currentUsn++);
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    thirdNotebook.setModificationTimestamp(thirdNotebook.creationTimestamp());
    thirdNotebook.setDefaultNotebook(false);
    thirdNotebook.setLastUsed(true);

    error.clear();
    bool res = localStorageManager.addNotebook(firstNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addNotebook(secondNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addNotebook(thirdNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 4) ========== Verify the current value of the account high USN ==========

    error.clear();
    qint32 accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != thirdNotebook.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(thirdNotebook.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 5) ========== Create some user's own tags with different USNs ==========

    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setUpdateSequenceNumber(currentUsn++);

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setUpdateSequenceNumber(currentUsn++);

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setUpdateSequenceNumber(currentUsn++);
    thirdTag.setParentGuid(secondTag.guid());
    thirdTag.setParentLocalUid(secondTag.localUid());

    error.clear();
    res = localStorageManager.addTag(firstTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(secondTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(thirdTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 6) ========== Verify the current value of the account high USN ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != thirdTag.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(thirdTag.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 7) ========== Create some user's own notes with different USNs ==========

    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setUpdateSequenceNumber(currentUsn++);
    firstNote.setNotebookLocalUid(firstNotebook.localUid());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstNote.setModificationTimestamp(firstNote.creationTimestamp());

    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setUpdateSequenceNumber(currentUsn++);
    secondNote.setNotebookLocalUid(secondNotebook.localUid());
    secondNote.setNotebookGuid(secondNotebook.guid());
    secondNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondNote.setModificationTimestamp(secondNote.creationTimestamp());

    error.clear();
    res = localStorageManager.addNote(firstNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addNote(secondNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 8) ========== Verify the current value of the account high USN ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != secondNote.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(secondNote.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 9) ========== Create one more note, this time with a resource which USN is higher than the note's one ==========

    Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setUpdateSequenceNumber(currentUsn++);
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setNotebookGuid(thirdNotebook.guid());
    thirdNote.setNotebookLocalUid(thirdNotebook.localUid());
    thirdNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setModificationTimestamp(thirdNote.creationTimestamp());

    Resource thirdNoteResource;
    thirdNoteResource.setGuid(UidGenerator::Generate());
    thirdNoteResource.setNoteGuid(thirdNote.guid());
    thirdNoteResource.setNoteLocalUid(thirdNote.localUid());
#if QT_VERSION > QT_VERSION_CHECK(5, 4, 0)
    thirdNoteResource.setDataBody(QByteArray::fromStdString(std::string("Something")));
#else
    thirdNoteResource.setDataBody(QByteArray("Something"));
#endif
    thirdNoteResource.setDataSize(thirdNoteResource.dataBody().size());
    thirdNoteResource.setDataHash(QCryptographicHash::hash(thirdNoteResource.dataBody(), QCryptographicHash::Md5));
    thirdNoteResource.setMime(QStringLiteral("text/plain"));
    thirdNoteResource.setUpdateSequenceNumber(currentUsn++);

    thirdNote.addResource(thirdNoteResource);

    error.clear();
    res = localStorageManager.addNote(thirdNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 10) ========== Verify the current value of the account high USN ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != thirdNoteResource.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(thirdNoteResource.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 11) ========== Create some user's own saved sarches with different USNs ==========

    SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First search"));
    firstSearch.setUpdateSequenceNumber(currentUsn++);
    firstSearch.setQuery(QStringLiteral("First"));

    SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second search"));
    secondSearch.setUpdateSequenceNumber(currentUsn++);
    secondSearch.setQuery(QStringLiteral("Second"));

    SavedSearch thirdSearch;
    thirdSearch.setGuid(UidGenerator::Generate());
    thirdSearch.setName(QStringLiteral("Third search"));
    thirdSearch.setUpdateSequenceNumber(currentUsn++);
    thirdSearch.setQuery(QStringLiteral("Third"));

    error.clear();
    res = localStorageManager.addSavedSearch(firstSearch, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addSavedSearch(secondSearch, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addSavedSearch(thirdSearch, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 12) ========== Verify the current value of the account high USN ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != thirdSearch.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(thirdSearch.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 13) ========== Create a linked notebook ==========

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(UidGenerator::Generate());
    linkedNotebook.setUpdateSequenceNumber(currentUsn++);
    linkedNotebook.setShareName(QStringLiteral("Share name"));
    linkedNotebook.setUsername(QStringLiteral("Username"));
    linkedNotebook.setShardId(UidGenerator::Generate());
    linkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    linkedNotebook.setUri(UidGenerator::Generate());

    error.clear();
    res = localStorageManager.addLinkedNotebook(linkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 14) ========== Verify the current value of the account high USN ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != linkedNotebook.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(linkedNotebook.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 15) ========== Add notebook and some tags and notes corresponding to the linked notebook ==========

    Notebook notebookFromLinkedNotebook;
    notebookFromLinkedNotebook.setGuid(linkedNotebook.sharedNotebookGlobalId());
    notebookFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    notebookFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);
    notebookFromLinkedNotebook.setName(QStringLiteral("Notebook from linked notebook"));
    notebookFromLinkedNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    notebookFromLinkedNotebook.setModificationTimestamp(notebookFromLinkedNotebook.creationTimestamp());

    Tag firstTagFromLinkedNotebook;
    firstTagFromLinkedNotebook.setGuid(UidGenerator::Generate());
    firstTagFromLinkedNotebook.setName(QStringLiteral("First tag from linked notebook"));
    firstTagFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    firstTagFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    Tag secondTagFromLinkedNotebook;
    secondTagFromLinkedNotebook.setGuid(UidGenerator::Generate());
    secondTagFromLinkedNotebook.setName(QStringLiteral("Second tag from linked notebook"));
    secondTagFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    secondTagFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    Note firstNoteFromLinkedNotebook;
    firstNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    firstNoteFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);
    firstNoteFromLinkedNotebook.setNotebookGuid(notebookFromLinkedNotebook.guid());
    firstNoteFromLinkedNotebook.setNotebookLocalUid(notebookFromLinkedNotebook.localUid());
    firstNoteFromLinkedNotebook.setTitle(QStringLiteral("First note from linked notebook"));
    firstNoteFromLinkedNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstNoteFromLinkedNotebook.setModificationTimestamp(firstNoteFromLinkedNotebook.creationTimestamp());
    firstNoteFromLinkedNotebook.addTagLocalUid(firstTagFromLinkedNotebook.localUid());
    firstNoteFromLinkedNotebook.addTagGuid(firstTagFromLinkedNotebook.guid());

    Note secondNoteFromLinkedNotebook;
    secondNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    secondNoteFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);
    secondNoteFromLinkedNotebook.setNotebookGuid(notebookFromLinkedNotebook.guid());
    secondNoteFromLinkedNotebook.setNotebookLocalUid(notebookFromLinkedNotebook.localUid());
    secondNoteFromLinkedNotebook.setTitle(QStringLiteral("Second note from linked notebook"));
    secondNoteFromLinkedNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondNoteFromLinkedNotebook.setModificationTimestamp(secondNoteFromLinkedNotebook.creationTimestamp());

    Resource secondNoteFromLinkedNotebookResource;
    secondNoteFromLinkedNotebookResource.setGuid(UidGenerator::Generate());
    secondNoteFromLinkedNotebookResource.setNoteGuid(secondNoteFromLinkedNotebook.guid());
    secondNoteFromLinkedNotebookResource.setNoteLocalUid(secondNoteFromLinkedNotebook.localUid());
#if QT_VERSION > QT_VERSION_CHECK(5, 4, 0)
    secondNoteFromLinkedNotebookResource.setDataBody(QByteArray::fromStdString(std::string("Other something")));
#else
    secondNoteFromLinkedNotebookResource.setDataBody(QByteArray("Other something"));
#endif
    secondNoteFromLinkedNotebookResource.setDataSize(secondNoteFromLinkedNotebookResource.dataBody().size());
    secondNoteFromLinkedNotebookResource.setDataHash(QCryptographicHash::hash(secondNoteFromLinkedNotebookResource.dataBody(), QCryptographicHash::Md5));
    secondNoteFromLinkedNotebookResource.setUpdateSequenceNumber(currentUsn++);

    secondNoteFromLinkedNotebook.addResource(secondNoteFromLinkedNotebookResource);

    error.clear();
    res = localStorageManager.addNotebook(notebookFromLinkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(firstTagFromLinkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(secondTagFromLinkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addNote(firstNoteFromLinkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addNote(secondNoteFromLinkedNotebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 16) ========== Verify the current value of the account high USN for user's own stuff ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(QString(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != linkedNotebook.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(linkedNotebook.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    // 17) ========== Verify the current value of the account high USN for the linked notebook ==========

    error.clear();
    accountHighUsn = localStorageManager.accountHighUsn(linkedNotebook.guid(), error);
    if (accountHighUsn < 0) {
        errorDescription = error.nonLocalizedString();
        return false;
    }
    else if (accountHighUsn != secondNoteFromLinkedNotebookResource.updateSequenceNumber()) {
        errorDescription = QStringLiteral("Wrong value of account high USN, expected ");
        errorDescription += QString::number(secondNoteFromLinkedNotebookResource.updateSequenceNumber());
        errorDescription += QStringLiteral(", got ");
        errorDescription += QString::number(accountHighUsn);
        return false;
    }

    return true;
}

bool TestAddingNoteWithoutLocalUid(QString & errorDescription)
{
    // 1) ========== Create LocalStorageManager =============

    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"), Account::Type::Evernote, 0);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    ErrorString error;

    // 2) ========== Add a notebook in order to test adding notes ==========

    Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    bool res = localStorageManager.addNotebook(notebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 3) ========== Try to add a note without local uid without tags or resources ===========
    Note firstNote;
    firstNote.unsetLocalUid();
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    error.clear();
    res = localStorageManager.addNote(firstNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    if (firstNote.localUid().isEmpty()) {
        errorDescription = QStringLiteral("Note local uid is empty after LocalStorageManager::addNote method returning");
        return false;
    }

    // 4) ========== Add some tags in order to test adding notes with tags ==========
    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third"));

    error.clear();
    res = localStorageManager.addTag(firstTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(secondTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(thirdTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 5) ========== Try to add a note without local uid with tag guids  ==========
    Note secondNote;
    secondNote.unsetLocalUid();
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));
    secondNote.addTagGuid(firstTag.guid());
    secondNote.addTagGuid(secondTag.guid());
    secondNote.addTagGuid(thirdTag.guid());

    error.clear();
    res = localStorageManager.addNote(secondNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 6) ========== Try to add a note without local uid with tag guids and with resources ==========
    Note thirdNote;
    thirdNote.unsetLocalUid();
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(notebook.guid());
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setContent(QStringLiteral("<en-note>third note</en-note>"));
    thirdNote.addTagGuid(firstTag.guid());
    thirdNote.addTagGuid(secondTag.guid());
    thirdNote.addTagGuid(thirdTag.guid());

    Resource resource;
    resource.setGuid(UidGenerator::Generate());
    resource.setNoteGuid(thirdNote.guid());
#if QT_VERSION > QT_VERSION_CHECK(5, 4, 0)
    QByteArray dataBody = QByteArray::fromStdString(std::string("Data"));
#else
    QByteArray dataBody = QByteArray("Data");
#endif
    resource.setDataBody(dataBody);
    resource.setDataSize(dataBody.size());
    resource.setDataHash(QCryptographicHash::hash(dataBody, QCryptographicHash::Md5));
    resource.setMime(QStringLiteral("text/plain"));

    thirdNote.addResource(resource);

    error.clear();
    res = localStorageManager.addNote(thirdNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    return true;
}

bool TestNoteTagIdsComplementWhenAddingAndUpdatingNote(QString & errorDescription)
{
    // 1) ========== Create LocalStorageManager =============

    const bool startFromScratch = true;
    const bool overrideLock = false;
    Account account(QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"), Account::Type::Evernote, 0);
    LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

    ErrorString error;

    // 2) ========== Add a notebook in order to test adding notes ==========

    Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    bool res = localStorageManager.addNotebook(notebook, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 3) ========== Add some tags ==========
    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third"));

    error.clear();
    res = localStorageManager.addTag(firstTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(secondTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    error.clear();
    res = localStorageManager.addTag(thirdTag, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    // 4) ========== Add a note without tag local uids but with tag guids ===========
    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    firstNote.addTagGuid(firstTag.guid());
    firstNote.addTagGuid(secondTag.guid());
    firstNote.addTagGuid(thirdTag.guid());

    error.clear();
    res = localStorageManager.addNote(firstNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    if (!firstNote.hasTagLocalUids()) {
        errorDescription = QStringLiteral("Note has no tag local uids after LocalStorageManager::addNote method returning");
        return false;
    }

    const QStringList & tagLocalUids = firstNote.tagLocalUids();
    if (tagLocalUids.size() != 3) {
        errorDescription = QStringLiteral("Note's tag local uids have improper size not matching the number of tag guids "
                                          "after LocalStorageManager::addNote method returning");
        return false;
    }

    if (!tagLocalUids.contains(firstTag.localUid()) ||
        !tagLocalUids.contains(secondTag.localUid()) ||
        !tagLocalUids.contains(thirdTag.localUid()))
    {
        errorDescription = QStringLiteral("Note doesn't have one of tag local uids it should have after LocalStorageManager::addNote method returning");
        return false;
    }

    // 5) ========== Add a note without tag guids but with tag local uids ===========
    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));

    secondNote.addTagLocalUid(firstTag.localUid());
    secondNote.addTagLocalUid(secondTag.localUid());
    secondNote.addTagLocalUid(thirdTag.localUid());

    error.clear();
    res = localStorageManager.addNote(secondNote, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    if (!secondNote.hasTagGuids()) {
        errorDescription = QStringLiteral("Note has no tag guids after LocalStorageManager::addNote method returning");
        return false;
    }

    const QStringList & tagGuids = secondNote.tagGuids();
    if (tagGuids.size() != 3) {
        errorDescription = QStringLiteral("Note's tag guids have improper size not matching the number of tag local uids "
                                          "after LocalStorageManager::addNote method returning");
        return false;
    }

    if (!tagGuids.contains(firstTag.guid()) ||
        !tagGuids.contains(secondTag.guid()) ||
        !tagGuids.contains(thirdTag.guid()))
    {
        errorDescription = QStringLiteral("Note doesn't have one of tag guids it should have after LocalStorageManager::addNote method returning");
        return false;
    }

    // 6) ========== Update note with tag guids ===========
    firstNote.setTitle(QStringLiteral("Updated first note"));
    firstNote.setTagLocalUids(QStringList());
    firstNote.setTagGuids(QStringList() << firstTag.guid() << secondTag.guid());

    error.clear();
    res = localStorageManager.updateNote(firstNote, /* update resources = */ false, /* update tags = */ true, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    if (!firstNote.hasTagLocalUids()) {
        errorDescription = QStringLiteral("Note has no tag local uids after LocalStorageManager::updateNote method returning");
        return false;
    }

    const QStringList & updatedTagLocalUids = firstNote.tagLocalUids();
    if (updatedTagLocalUids.size() != 2) {
        errorDescription = QStringLiteral("Note's tag local uids have improper size not matching the number of tag guids "
                                          "after LocalStorageManager::updateNote method returning");
        return false;
    }

    if (!updatedTagLocalUids.contains(firstTag.localUid()) || !updatedTagLocalUids.contains(secondTag.localUid())) {
        errorDescription = QStringLiteral("Note doesn't have one of tag local uids it should have after LocalStorageManager::updateNote method returning");
        return false;
    }

    // 7) ========== Update note with tag guids ===========
    secondNote.setTitle(QStringLiteral("Updated second note"));
    secondNote.setTagGuids(QStringList());
    secondNote.setTagLocalUids(QStringList() << firstTag.localUid() << secondTag.localUid());

    error.clear();
    res = localStorageManager.updateNote(secondNote, /* update resources = */ false, /* update tags = */ true, error);
    if (!res) {
        errorDescription = error.nonLocalizedString();
        return false;
    }

    if (!secondNote.hasTagGuids()) {
        errorDescription = QStringLiteral("Note has no tag guids after LocalStorageManager::updateNote method returning");
        return false;
    }

    const QStringList & updatedTagGuids = secondNote.tagGuids();
    if (updatedTagGuids.size() != 2) {
        errorDescription = QStringLiteral("Note's tag guids have improper size not matching the number of tag local uids "
                                          "after LocalStorageManager::updateNote method returning");
        return false;
    }

    if (!updatedTagGuids.contains(firstTag.guid()) || !updatedTagGuids.contains(secondTag.guid())) {
        errorDescription = QStringLiteral("Note doesn't have one of tag guids it should have after LocalStorageManager::updateNote method returning");
        return false;
    }

    return true;
}

} // namespace test
} // namespace quentier
