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

#include "LocalStorageManagerBasicTests.h"

#include "../TestMacros.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>
#include <quentier/utility/UidGenerator.h>

#include <QCryptographicHash>
#include <QtTest/QtTest>

#include <string>

namespace quentier {
namespace test {

void TestSavedSearchAddFindUpdateExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

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

    QVERIFY2(
        search.checkParameters(errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addSavedSearch(search, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString searchGuid = search.localUid();
    SavedSearch foundSearch;
    foundSearch.setLocalUid(searchGuid);

    QVERIFY2(
        localStorageManager.findSavedSearch(foundSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        search == foundSearch,
        "Added and found saved searches in the local storage don't match: "
            << "saved search added to the local storage: " << search
            << "\nSaved search found in the local storage:" << foundSearch);

    // Check Find by name
    SavedSearch foundByNameSearch;
    foundByNameSearch.unsetLocalUid();
    foundByNameSearch.setName(search.name());

    QVERIFY2(
        localStorageManager.findSavedSearch(foundByNameSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        search == foundByNameSearch,
        "Added and found by name saved searches in the local storage don't "
            << "match: saved search added to the local storage: " << search
            << "\nSaved search found by name in the local storage: "
            << foundByNameSearch);

    // Check Update + Find
    SavedSearch modifiedSearch(search);
    modifiedSearch.setUpdateSequenceNumber(search.updateSequenceNumber() + 1);
    modifiedSearch.setName(search.name() + QStringLiteral("_modified"));
    modifiedSearch.setQuery(search.query() + QStringLiteral("_modified"));
    modifiedSearch.setFavorited(true);
    modifiedSearch.setDirty(true);

    QString localUid = modifiedSearch.localUid();
    modifiedSearch.unsetLocalUid();

    QVERIFY2(
        localStorageManager.updateSavedSearch(modifiedSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.findSavedSearch(foundSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    modifiedSearch.setLocalUid(localUid);
    VERIFY2(
        modifiedSearch == foundSearch,
        "Updated and found saved searches in the local storage "
            << "don't match: saved search updated in the local storage: "
            << modifiedSearch
            << "\nSavedSearch found in the local storage: " << foundSearch);

    // Check savedSearchCount to return 1
    int count = localStorageManager.savedSearchCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("GetSavedSearchCount returned result %1 "
                                     "different from the expected one (1)")
                       .arg(count)));

    // Check Expunge + Find (failure expected)
    QVERIFY2(
        localStorageManager.expungeSavedSearch(modifiedSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !localStorageManager.findSavedSearch(foundSearch, errorMessage),
        "Error: found saved search which should have been "
            << "expunged from local storage: saved search expunged "
            << "from the local storage: " << modifiedSearch
            << "\nSavedSearch found in the local storage: " << foundSearch);

    // Check savedSearchCount to return 0
    count = localStorageManager.savedSearchCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        count == 0,
        QString::fromUtf8("savedSearchCount returned result %1 "
                          "different from the expected one (0)")
            .arg(count));
}

void TestFindSavedSearchByNameWithDiacritics()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("TestFindSavedSearchByNameWithDiacriticsFakeUser"),
        Account::Type::Local);

    LocalStorageManager localStorageManager(account, startupOptions);

    SavedSearch search1;
    search1.setGuid(UidGenerator::Generate());
    search1.setUpdateSequenceNumber(1);
    search1.setName(QStringLiteral("search"));

    SavedSearch search2;
    search2.setGuid(UidGenerator::Generate());
    search2.setUpdateSequenceNumber(2);
    search2.setName(QStringLiteral("séarch"));

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addSavedSearch(search1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addSavedSearch(search2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    SavedSearch searchToFind;
    searchToFind.unsetLocalUid();
    searchToFind.setName(search1.name());

    QVERIFY2(
        localStorageManager.findSavedSearch(searchToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        searchToFind == search1,
        "Found wrong saved search by name: expected saved search: "
            << search1 << "\nActually found search: " << searchToFind);

    searchToFind = SavedSearch();
    searchToFind.unsetLocalUid();
    searchToFind.setName(search2.name());

    QVERIFY2(
        localStorageManager.findSavedSearch(searchToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        searchToFind == search2,
        "Found wrong saved search by name: expected saved search: "
            << search2 << "\nActually found search: " << searchToFind);
}

void TestLinkedNotebookAddFindUpdateExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

    LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000046"));

    linkedNotebook.setUpdateSequenceNumber(1);

    linkedNotebook.setShareName(
        QStringLiteral("Fake linked notebook share name"));

    linkedNotebook.setUsername(QStringLiteral("Fake linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Fake linked notebook shard id"));

    linkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("Fake linked notebook shared notebook global id"));

    linkedNotebook.setUri(QStringLiteral("Fake linked notebook uri"));

    linkedNotebook.setNoteStoreUrl(
        QStringLiteral("Fake linked notebook note store url"));

    linkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Fake linked notebook web api url prefix"));

    linkedNotebook.setStack(QStringLiteral("Fake linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    ErrorString errorMessage;

    VERIFY2(
        linkedNotebook.checkParameters(errorMessage),
        "Found invalid LinkedNotebook: " << linkedNotebook << ", error: "
                                         << errorMessage.nonLocalizedString());

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString linkedNotebookGuid = linkedNotebook.guid();
    LinkedNotebook foundLinkedNotebook;
    foundLinkedNotebook.setGuid(linkedNotebookGuid);

    QVERIFY2(
        localStorageManager.findLinkedNotebook(
            foundLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        linkedNotebook == foundLinkedNotebook,
        "Added and found linked noteboks in the local storage "
            << "don't match: LinkedNotebook added to the local storage: "
            << linkedNotebook << "\nLinkedNotebook found in the local storage: "
            << foundLinkedNotebook);

    // Check Update + Find
    LinkedNotebook modifiedLinkedNotebook(linkedNotebook);

    modifiedLinkedNotebook.setUpdateSequenceNumber(
        linkedNotebook.updateSequenceNumber() + 1);

    modifiedLinkedNotebook.setShareName(
        linkedNotebook.shareName() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setUsername(
        linkedNotebook.username() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setShardId(
        linkedNotebook.shardId() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setSharedNotebookGlobalId(
        linkedNotebook.sharedNotebookGlobalId() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setUri(
        linkedNotebook.uri() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setNoteStoreUrl(
        linkedNotebook.noteStoreUrl() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setWebApiUrlPrefix(
        linkedNotebook.webApiUrlPrefix() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setStack(
        linkedNotebook.stack() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setBusinessId(linkedNotebook.businessId() + 1);

    QVERIFY2(
        localStorageManager.updateLinkedNotebook(
            modifiedLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.findLinkedNotebook(
            foundLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        modifiedLinkedNotebook == foundLinkedNotebook,
        "Updated and found linked notebooks in the local storage "
            << "don't match: LinkedNotebook updated in the local storage: "
            << modifiedLinkedNotebook
            << "\nLinkedNotebook found in the local storage: "
            << foundLinkedNotebook);

    // Check linkedNotebookCount to return 1
    int count = localStorageManager.linkedNotebookCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("linkedNotebookCount returned result %1 "
                                     "different from the expected one (1)")
                       .arg(count)));

    // Check Expunge + Find (failure expected)
    QVERIFY2(
        localStorageManager.expungeLinkedNotebook(
            modifiedLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !localStorageManager.findLinkedNotebook(
            foundLinkedNotebook, errorMessage),
        "Error: found linked notebook which should have been expunged from "
            << "the local storage: LinkedNotebook expunged "
            << "from the local storage: " << modifiedLinkedNotebook
            << "\nLinkedNotebook found in the local storage: "
            << foundLinkedNotebook);

    // Check linkedNotebookCount to return 0
    count = localStorageManager.linkedNotebookCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 0,
        qPrintable(
            QString::fromUtf8("GetLinkedNotebookCount returned result %1 "
                              "different from the expected one (0)")
                .arg(count)));
}

void TestTagAddFindUpdateExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

    LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNumber(1);
    linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));

    linkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("Linked notebook shared notebook global id"));

    linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));

    linkedNotebook.setNoteStoreUrl(
        QStringLiteral("Linked notebook note store url"));

    linkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Linked notebook web api url prefix"));

    linkedNotebook.setStack(QStringLiteral("Linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    ErrorString errorMessage;
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setLinkedNotebookGuid(linkedNotebook.guid());
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    QVERIFY2(
        tag.checkParameters(errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString localTagGuid = tag.localUid();
    Tag foundTag;
    foundTag.setLocalUid(localTagGuid);
    if (tag.hasLinkedNotebookGuid()) {
        foundTag.setLinkedNotebookGuid(tag.linkedNotebookGuid());
    }

    QVERIFY2(
        localStorageManager.findTag(foundTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tag == foundTag,
        "Added and found tags in the local storage don't match: "
            << "tag added to the local storage: " << tag
            << "\nTag found in the local storage: " << foundTag);

    // Check Find by name
    Tag foundByNameTag;
    foundByNameTag.unsetLocalUid();
    foundByNameTag.setName(tag.name());
    if (tag.hasLinkedNotebookGuid()) {
        foundByNameTag.setLinkedNotebookGuid(tag.linkedNotebookGuid());
    }

    QVERIFY2(
        localStorageManager.findTag(foundByNameTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tag == foundByNameTag,
        "Tag found by name in the local storage doesn't match "
            << "the original tag: tag found by name: " << foundByNameTag
            << "\nOriginal tag: " << tag);

    // Check Update + Find
    Tag modifiedTag(tag);
    modifiedTag.setUpdateSequenceNumber(tag.updateSequenceNumber() + 1);
    modifiedTag.setLinkedNotebookGuid(QLatin1String(""));
    modifiedTag.setName(tag.name() + QStringLiteral("_modified"));
    modifiedTag.setFavorited(true);
    modifiedTag.unsetLocalUid();

    QVERIFY2(
        localStorageManager.updateTag(modifiedTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    if (!modifiedTag.hasLinkedNotebookGuid()) {
        foundTag.setLinkedNotebookGuid(QLatin1String(""));
    }

    QVERIFY2(
        localStorageManager.findTag(foundTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    modifiedTag.setLocalUid(localTagGuid);

    VERIFY2(
        modifiedTag == foundTag,
        "Updated and found tags in the local storage don't match: "
            << ": Tag updated in the local storage: " << modifiedTag
            << "\nTag found in the local storage: " << foundTag);

    // tagCount to return 1
    int count = localStorageManager.tagCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        count == 1,
        QString::fromUtf8("tagCount returned result %1 different "
                          "from the expected one (1)")
            .arg(count));

    // Add another tag referencing the first tag as its parent
    Tag newTag;
    newTag.setName(QStringLiteral("New tag"));
    newTag.setParentGuid(tag.guid());
    newTag.setParentLocalUid(tag.localUid());

    QVERIFY2(
        localStorageManager.addTag(newTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Tag foundNewTag;
    foundNewTag.setLocalUid(newTag.localUid());

    QVERIFY2(
        localStorageManager.findTag(foundNewTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        newTag == foundNewTag,
        "Second added tag and its found copy "
            << "from the local storage don't match"
            << ": the second tag added to the local storage: " << newTag
            << "\nTag found in the local storage: " << foundNewTag);

    // Check Expunge + Find (failure expected)
    QStringList expungedChildTagLocalUids;

    QVERIFY2(
        localStorageManager.expungeTag(
            modifiedTag, expungedChildTagLocalUids, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Q_UNUSED(expungedChildTagLocalUids)

    VERIFY2(
        !localStorageManager.findTag(foundTag, errorMessage),
        "Error: found tag which should have "
            << "been expunged from the local storage"
            << ": Tag expunged from the local storage: " << modifiedTag
            << "\nTag found in the local storage: " << foundTag);
}

void TestFindTagByNameWithDiacritics()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("TestFindTagByNameWithDiacriticsFakeUser"),
        Account::Type::Local);

    LocalStorageManager localStorageManager(account, startupOptions);

    Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNumber(1);
    tag1.setName(QStringLiteral("tag"));

    Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNumber(2);
    tag2.setName(QStringLiteral("tāg"));

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addTag(tag1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addTag(tag2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Tag tagToFind;
    tagToFind.unsetLocalUid();
    tagToFind.setName(tag1.name());

    QVERIFY2(
        localStorageManager.findTag(tagToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tagToFind == tag1,
        "Found wrong tag by name: expected tag: "
            << tag1 << "\nActually found tag: " << tagToFind);

    tagToFind = Tag();
    tagToFind.unsetLocalUid();
    tagToFind.setName(tag2.name());

    QVERIFY2(
        localStorageManager.findTag(tagToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tagToFind == tag2,
        "Found wrong tag by name: expected tag: "
            << tag2 << "\nActually found tag: " << tagToFind);
}

void TestResourceAddFindUpdateExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

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

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    resource.setUpdateSequenceNumber(1);
    resource.setNoteGuid(note.guid());
    resource.setDataBody(QByteArray("Fake resource data body"));
    resource.setDataSize(resource.dataBody().size());
    resource.setDataHash(QByteArray("Fake hash      1"));

    resource.setRecognitionDataBody(
        QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" "
                   "objID=\"fc83e58282d8059be17debabb69be900\" "
                   "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                   "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
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

    auto & resourceAttributes = resource.resourceAttributes();

    resourceAttributes.sourceURL = QStringLiteral("Fake resource source URL");
    resourceAttributes.timestamp = 1;
    resourceAttributes.latitude = 0.0;
    resourceAttributes.longitude = 0.0;
    resourceAttributes.altitude = 0.0;
    resourceAttributes.cameraMake = QStringLiteral("Fake resource camera make");

    resourceAttributes.cameraModel =
        QStringLiteral("Fake resource camera model");

    note.unsetLocalUid();

    QVERIFY2(
        resource.checkParameters(errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addEnResource(resource, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString resourceGuid = resource.guid();
    Resource foundResource;
    foundResource.setGuid(resourceGuid);

    LocalStorageManager::GetResourceOptions getResourceOptions(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        resource == foundResource,
        "Added and found in the local storage "
            << "resources don't match"
            << ": Resource added to the local storage: " << resource
            << "\nIResource found in the local storage: " << foundResource);

    // Check Update + Find
    Resource modifiedResource(resource);

    modifiedResource.setUpdateSequenceNumber(
        resource.updateSequenceNumber() + 1);

    modifiedResource.setDataBody(resource.dataBody() + QByteArray("_modified"));
    modifiedResource.setDataSize(modifiedResource.dataBody().size());
    modifiedResource.setDataHash(QByteArray("Fake hash      3"));

    modifiedResource.setWidth(resource.width() + 1);
    modifiedResource.setHeight(resource.height() + 1);

    modifiedResource.setRecognitionDataBody(
        QByteArray("<recoIndex docType=\"picture\" objType=\"image\" "
                   "objID=\"fc83e58282d8059be17debabb69be900\" "
                   "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                   "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
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

    modifiedResource.setRecognitionDataSize(
        modifiedResource.recognitionDataBody().size());

    modifiedResource.setRecognitionDataHash(QByteArray("Fake hash      4"));

    modifiedResource.setAlternateDataBody(
        resource.alternateDataBody() + QByteArray("_modified"));

    modifiedResource.setAlternateDataSize(
        modifiedResource.alternateDataBody().size());

    modifiedResource.setAlternateDataHash(QByteArray("Fake hash      5"));

    auto & modifiedResourceAttributes = modifiedResource.resourceAttributes();

    modifiedResourceAttributes.sourceURL =
        QStringLiteral("Modified source URL");

    modifiedResourceAttributes.timestamp += 1;
    modifiedResourceAttributes.latitude = 2.0;
    modifiedResourceAttributes.longitude = 2.0;
    modifiedResourceAttributes.altitude = 2.0;

    modifiedResourceAttributes.cameraMake =
        QStringLiteral("Modified camera make");

    modifiedResourceAttributes.cameraModel =
        QStringLiteral("Modified camera model");

    modifiedResource.unsetLocalUid();

    QVERIFY2(
        localStorageManager.updateEnResource(modifiedResource, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        modifiedResource == foundResource,
        "Updated and found in the local storage resources don't match: "
            << "Resource updated in the local storage: " << modifiedResource
            << "\nIResource found in the local storage: " << foundResource);

    // Check Find without resource binary data
    foundResource.clear();
    foundResource.setGuid(resourceGuid);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    getResourceOptions = LocalStorageManager::GetResourceOptions();
#else
    getResourceOptions = LocalStorageManager::GetResourceOptions(0);
#endif

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    modifiedResource.setDataBody(QByteArray());
    modifiedResource.setAlternateDataBody(QByteArray());

    VERIFY2(
        modifiedResource == foundResource,
        "Updated and found in the local storage "
        "resources without binary data don't match: "
            << "Resource updated in the local storage: " << modifiedResource
            << "\nIResource found in the local storage: " << foundResource);

    // enResourceCount to return 1
    int count = localStorageManager.enResourceCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("enResourceCount returned result %1 "
                                     "different from the expected one (1)")
                       .arg(count)));

    // Check Expunge + Find (falure expected)
    QVERIFY2(
        localStorageManager.expungeEnResource(modifiedResource, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        "Error: found Resource which should have "
            << "been expunged from the local storage"
            << ": Resource expunged from the local storage: "
            << modifiedResource
            << "\nIResource found in the local storage: " << foundResource);

    // enResourceCount to return 0
    count = localStorageManager.enResourceCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 0,
        qPrintable(QString::fromUtf8("enResourceCount returned result %1 "
                                     "different from the expected one (0)")
                       .arg(count)));
}

void TestNoteAddFindUpdateDeleteExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

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

    sharedNote.setRecipientIdentityContactType(
        qevercloud::ContactType::EVERNOTE);

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

    sharedNote.setPrivilegeLevel(
        qevercloud::SharedNotePrivilegeLevel::FULL_ACCESS);

    sharedNote.setCreationTimestamp(6);
    sharedNote.setModificationTimestamp(7);
    sharedNote.setAssignmentTimestamp(8);
    note.addSharedNote(sharedNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    note.addTagGuid(tag.guid());
    note.addTagLocalUid(tag.localUid());

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(note, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

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

    resource.setRecognitionDataBody(
        QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" "
                   "objID=\"fc83e58282d8059be17debabb69be900\" "
                   "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                   "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
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

    auto & resourceAttributes = resource.resourceAttributes();

    resourceAttributes.sourceURL = QStringLiteral("Fake resource source URL");
    resourceAttributes.timestamp = 1;
    resourceAttributes.latitude = 0.0;
    resourceAttributes.longitude = 0.0;
    resourceAttributes.altitude = 0.0;

    resourceAttributes.cameraMake = QStringLiteral("Fake resource camera make");

    resourceAttributes.cameraModel =
        QStringLiteral("Fake resource camera model");

    note.addResource(resource);

    errorMessage.clear();

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(note, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Find
    const QString initialResourceGuid =
        QStringLiteral("00000000-0000-0000-c000-000000000049");

    Resource foundResource;
    foundResource.setGuid(initialResourceGuid);

    LocalStorageManager::GetResourceOptions getResourceOptions(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString noteGuid = note.guid();

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    Note foundNote;
    foundNote.setGuid(noteGuid);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid
     * than the original note which doesn't have one. So use this workaround
     * to ensure the comparison is good for everything without local uid
     */
    if (note.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    VERIFY2(
        note == foundNote,
        "Added and found notes in the local storage don't match"
            << ": Note added to the local storage: " << note
            << "\nNote found in the local storage: " << foundNote);

    // Check Update + Find
    Note modifiedNote(note);
    modifiedNote.setUpdateSequenceNumber(note.updateSequenceNumber() + 1);
    modifiedNote.setTitle(note.title() + QStringLiteral("_modified"));
    modifiedNote.setCreationTimestamp(note.creationTimestamp() + 1);
    modifiedNote.setModificationTimestamp(note.modificationTimestamp() + 1);
    modifiedNote.setFavorited(true);

    auto & modifiedNoteAttributes = modifiedNote.noteAttributes();

    modifiedNoteAttributes.subjectDate = 2;
    modifiedNoteAttributes.latitude = 2.0;
    modifiedNoteAttributes.longitude = 2.0;
    modifiedNoteAttributes.altitude = 2.0;
    modifiedNoteAttributes.author = QStringLiteral("modified author");
    modifiedNoteAttributes.source = QStringLiteral("modified source");
    modifiedNoteAttributes.sourceURL = QStringLiteral("modified source URL");

    modifiedNoteAttributes.sourceApplication =
        QStringLiteral("modified source application");

    modifiedNoteAttributes.shareDate = 2;

    Tag newTag;
    newTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000050"));
    newTag.setUpdateSequenceNumber(1);
    newTag.setName(QStringLiteral("Fake new tag name"));

    QVERIFY2(
        localStorageManager.addTag(newTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

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

    newResource.setRecognitionDataBody(
        QByteArray("<recoIndex docType=\"picture\" objType=\"image\" "
                   "objID=\"fc83e58282d8059be17debabb69be900\" "
                   "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                   "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
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

    newResource.setRecognitionDataSize(
        newResource.recognitionDataBody().size());

    newResource.setRecognitionDataHash(QByteArray("Fake hash      4"));

    auto & newResourceAttributes = newResource.resourceAttributes();

    newResourceAttributes.sourceURL =
        QStringLiteral("Fake resource source URL");

    newResourceAttributes.timestamp = 1;
    newResourceAttributes.latitude = 0.0;
    newResourceAttributes.longitude = 0.0;
    newResourceAttributes.altitude = 0.0;

    newResourceAttributes.cameraMake =
        QStringLiteral("Fake resource camera make");

    newResourceAttributes.cameraModel =
        QStringLiteral("Fake resource camera model");

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

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundResource = Resource();
    foundResource.setGuid(newResource.guid());

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundResource.setNoteLocalUid(QString());

    VERIFY2(
        foundResource == newResource,
        "Something is wrong with the new resource "
            << "which should have been added to the local "
            << "storage along with the note update: it is "
            << "not equal to the original resource"
            << ": original resource: " << newResource
            << "\nfound resource: " << foundResource);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local uid
     */
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match"
            << ": Note updated in the local storage: " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Check that tags are not touched if update tags flag is not set on attempt
    // to update note
    QStringList tagLocalUidsBeforeUpdate = modifiedNote.tagLocalUids();
    QStringList tagGuidsBeforeUpdate = modifiedNote.tagGuids();

    modifiedNote.removeTagGuid(newTag.guid());
    modifiedNote.removeTagLocalUid(newTag.localUid());

    // Modify something about the note to make the test a little more
    // interesting
    modifiedNote.setTitle(
        modifiedNote.title() + QStringLiteral("_modified_again"));

    modifiedNote.setFavorited(false);
    modifiedNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local uid
     */
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    /**
     * Found note should not be equal to the modified note because their tag ids
     * should be different; after restoring the previous tag ids lists to
     * the modified note the two notes should become equal
     */
    VERIFY2(
        modifiedNote != foundNote,
        "Detected unexpectedly equal notes: "
            << "locally modified notes which had its "
            << "tags list modified but not updated "
            << "in the local storage and the note found "
            << "in the local storage"
            << ": Note updated in the local storage "
            << "(without tags lists): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    modifiedNote.setTagGuids(tagGuidsBeforeUpdate);
    modifiedNote.setTagLocalUids(tagLocalUidsBeforeUpdate);

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match"
            << ": Note updated in the local storage "
            << "(without tags after which tags were "
            << "manually restored): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    /**
     * Check that resources are not touched if update resource metadata flag
     * is not set on attempt to update note
     */
    QList<Resource> previousModifiedNoteResources = modifiedNote.resources();
    Q_UNUSED(modifiedNote.removeResource(newResource))

    // Modify something about the note to make the test a little more
    // interesting
    modifiedNote.setTitle(
        modifiedNote.title() + QStringLiteral("_modified_once_again"));

    modifiedNote.setFavorited(true);
    modifiedNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local uid
     */
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    /**
     * Found note should not be equal to the modified note because their
     * resources should be different; after restoring the previous resources
     * list to the modified note the two notes should become equal
     */
    VERIFY2(
        modifiedNote != foundNote,
        "Detected unexpectedly equal notes: "
            << "locally modified notes which had its "
            << "resources list modified but not updated "
            << "in the local storage and the note found "
            << "in the local storage"
            << ": Note updated in the local storage "
            << "(with resource removed): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    modifiedNote.setResources(previousModifiedNoteResources);

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match"
            << ": Note updated in the local storage "
            << "(without resource metadata after which "
            << "resources were manually restored): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    /**
     * Check that resources are not touched if update resource metadata flag
     * is not set even if update resource binary data flag is set on attempt
     * to update note
     */
    Q_UNUSED(modifiedNote.removeResource(newResource))

    modifiedNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local uid
     */
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    /**
     * Found note should not be equal to the modified note because their
     * resources should be different; after restoring the previous resources
     * list to the modified note the two notes should become equal
     */
    VERIFY2(
        modifiedNote != foundNote,
        "Detected unexpectedly equal notes: "
            << "locally modified notes which had its "
            << "resources list modified but not updated "
            << "in the local storage and the note found "
            << "in the local storage"
            << ": Note updated in the local storage "
            << "(with resource removed): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    modifiedNote.setResources(previousModifiedNoteResources);

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match"
            << ": Note updated in the local storage "
            << "(without resource metadata after which "
            << "resources were manually restored): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Check that resource binary data is not touched unless update resource
    // binary data flag is set on attempt to update note
    newResource.setDataBody(QByteArray("Fake modified new resource data body"));
    newResource.setDataSize(newResource.dataBody().size());

    Q_UNUSED(modifiedNote.updateResource(newResource))

    modifiedNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local uid is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local uid
     */
    if (modifiedNote.localUid().isEmpty()) {
        foundNote.unsetLocalUid();
    }

    /**
     * Found note should not be equal to the modified note because the binary
     * data of one resource should be different; after restoring the previous
     * resources to the modified note the two notes should become equal
     */
    VERIFY2(
        modifiedNote != foundNote,
        "Detected unexpectedly equal notes: "
            << "locally modified notes which had its "
            << "resource data body modified but not "
            << "updated in the local storage and "
            << "the note found in the local storage"
            << ": Note updated in the local storage (without "
            << "resource data body): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    modifiedNote.setResources(previousModifiedNoteResources);

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't "
            << "match: Note updated in the local storage "
            << "(without resource binary data after which "
            << "resources were manually restored): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Add one more note to test note counting methods
    Note newNote;
    newNote.setNotebookGuid(notebook.guid());
    newNote.setTitle(QStringLiteral("New note"));
    newNote.addTagGuid(tag.guid());
    newNote.addTagLocalUid(tag.localUid());

    QVERIFY2(
        localStorageManager.addNote(newNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // noteCount to return 2
    int count = localStorageManager.noteCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 2,
        qPrintable(QString::fromUtf8("noteCount returned result %1 different "
                                     "from the expected one (2)")
                       .arg(count)));

    // noteCountPerNotebook to return 2
    count = localStorageManager.noteCountPerNotebook(notebook, errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 2,
        qPrintable(QString::fromUtf8("noteCountPerNotebook returned result %1 "
                                     "different from the expected one (2)")
                       .arg(count)));

    // noteCountPerTag to return 1 for new tag
    count = localStorageManager.noteCountPerTag(newTag, errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("noteCountPerTag returned result %1 "
                                     "different from the expected one (1)")
                       .arg(count)));

    // noteCountPerTag to return 2 for old tag
    count = localStorageManager.noteCountPerTag(tag, errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 2,
        qPrintable(QString::fromUtf8("noteCountPerTag returned result %1 "
                                     "different from the expected one (2)")
                       .arg(count)));

    // Note count per all tags to return 2 and 1 for first and second tags
    QHash<QString, int> noteCountsPerTagLocalUid;

    QVERIFY2(
        localStorageManager.noteCountsPerAllTags(
            noteCountsPerTagLocalUid, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        noteCountsPerTagLocalUid.size() == 2,
        qPrintable(QString::fromUtf8("Unexpected amount of tag local uids "
                                     "within the hash of note counts by tag "
                                     "local uid: expected 2, got %1")
                       .arg(noteCountsPerTagLocalUid.size())));

    auto firstTagNoteCountIt = noteCountsPerTagLocalUid.find(tag.localUid());

    QVERIFY2(
        firstTagNoteCountIt != noteCountsPerTagLocalUid.end(),
        qPrintable(QStringLiteral(
            "Can't find the note count for first tag's local uid")));

    QVERIFY2(
        firstTagNoteCountIt.value() == 2,
        qPrintable(QString::fromUtf8("Unexpected note count for the first "
                                     "tag: expected 2, got %1")
                       .arg(firstTagNoteCountIt.value())));

    auto secondTagNoteCountIt =
        noteCountsPerTagLocalUid.find(newTag.localUid());

    QVERIFY2(
        secondTagNoteCountIt != noteCountsPerTagLocalUid.end(),
        qPrintable(QStringLiteral(
            "Can't find the note count for second tag's local uid")));

    QVERIFY2(
        secondTagNoteCountIt.value() == 1,
        qPrintable(
            QString::fromUtf8(
                "Unexpected note count for the second tag: expected 1, got %1")
                .arg(secondTagNoteCountIt.value())));

    // noteCountPerNotebooksAndTags to return 1 for new tag
    QStringList notebookLocalUids;
    notebookLocalUids << notebook.localUid();
    QStringList tagLocalUids;
    tagLocalUids << newTag.localUid();

    count = localStorageManager.noteCountPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, errorMessage);

    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("noteCountPerNotebooksAndTags returned "
                                     "result %1 different from the expected "
                                     "one (1)")
                       .arg(count)));

    // noteCountPerNotebooksAndTags to return 2 for old tag
    tagLocalUids << tag.localUid();

    count = localStorageManager.noteCountPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, errorMessage);

    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 2,
        qPrintable(QString::fromUtf8("noteCountPerNotebooksAndTags returned "
                                     "result %1 different from the expected "
                                     "one (2)")
                       .arg(count)));

    // Check Delete + Find and check deleted flag
    modifiedNote.setActive(false);
    modifiedNote.setDeletionTimestamp(1);
    foundNote.setActive(true);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    updateNoteOptions = LocalStorageManager::UpdateNoteOptions();
#else
    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(0);
#endif

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        foundNote.hasActive() && !foundNote.active(),
        "Note which should have been marked "
            << "non-active is not marked so after "
            << "LocalStorageManager::FindNote: "
            << "Note found in the local storage: " << foundNote);

    // noteCount to return 1
    count = localStorageManager.noteCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("noteCount returned result %1 different "
                                     "from the expected one (1)")
                       .arg(count)));

    // Check Expunge + Find (failure expected)
    QVERIFY2(
        localStorageManager.expungeNote(modifiedNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        "Error: found Note which should have "
            << "been expunged from the local storage: "
            << "Note expunged from the local storage: " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Try to find resource belonging to expunged note (failure expected)
    foundResource = Resource();
    foundResource.setGuid(newResource.guid());

    VERIFY2(
        !localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        "Error: found Resource which should "
            << "have been expunged from the local storage "
            << "along with Note owning it: "
            << "Note expunged from the local storage: " << modifiedNote
            << "\nResource found in the local storage: " << foundResource);
}

void TestNotebookAddFindUpdateDeleteExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

    LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNumber(1);
    linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));

    linkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("Linked notebook shared notebook global id"));

    linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));

    linkedNotebook.setNoteStoreUrl(
        QStringLiteral("Linked notebook note store url"));

    linkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Linked notebook web api url prefix"));

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

    notebook.setPublishingPublicDescription(
        QStringLiteral("Fake public description"));

    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    notebook.setBusinessNotebookDescription(
        QStringLiteral("Fake business notebook description"));

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

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilegeLevel(1);
    sharedNotebook.setReminderNotifyEmail(true);
    sharedNotebook.setReminderNotifyApp(false);

    notebook.addSharedNotebook(sharedNotebook);

    ErrorString errorMessage;
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

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

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    note.addTagGuid(tag.guid());
    note.addTagLocalUid(tag.localUid());

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(note, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        notebook.checkParameters(errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Find
    const QString initialNoteGuid =
        QStringLiteral("00000000-0000-0000-c000-000000000049");

    Note foundNote;
    foundNote.setGuid(initialNoteGuid);

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());
    if (notebook.hasLinkedNotebookGuid()) {
        foundNotebook.setLinkedNotebookGuid(notebook.linkedNotebookGuid());
    }

    QVERIFY2(
        localStorageManager.findNotebook(foundNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        notebook == foundNotebook,
        "Added and found notebooks in local storage don't match: "
            << "Notebook added to LocalStorageManager: " << notebook
            << "\nNotebook found in LocalStorageManager: " << foundNotebook);

    // Check Find by name
    Notebook foundByNameNotebook;
    foundByNameNotebook.unsetLocalUid();
    foundByNameNotebook.setName(notebook.name());
    if (notebook.hasLinkedNotebookGuid()) {
        foundByNameNotebook.setLinkedNotebookGuid(
            notebook.linkedNotebookGuid());
    }

    QVERIFY2(
        localStorageManager.findNotebook(foundByNameNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        notebook == foundByNameNotebook,
        "Notebook found by name in local storage "
            << "doesn't match the original notebook: "
            << "Notebook found by name: " << foundByNameNotebook
            << "\nOriginal notebook: " << notebook);

    if (notebook.hasLinkedNotebookGuid()) {
        // Check Find by linked notebook guid
        Notebook foundByLinkedNotebookGuidNotebook;
        foundByLinkedNotebookGuidNotebook.unsetLocalUid();

        foundByLinkedNotebookGuidNotebook.setLinkedNotebookGuid(
            notebook.linkedNotebookGuid());

        QVERIFY2(
            localStorageManager.findNotebook(
                foundByLinkedNotebookGuidNotebook, errorMessage),
            qPrintable(errorMessage.nonLocalizedString()));

        VERIFY2(
            notebook == foundByLinkedNotebookGuidNotebook,
            "Notebook found by linked notebook guid in "
            "the local storage doesn't match the original "
            "notebook: notebook found by linked notebook guid: "
                << foundByLinkedNotebookGuidNotebook
                << "\nOriginal notebook: " << notebook);
    }

    // Check FindDefaultNotebook
    Notebook defaultNotebook;

    QVERIFY2(
        localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check FindLastUsedNotebook (failure expected)
    Notebook lastUsedNotebook;

    VERIFY2(
        !localStorageManager.findLastUsedNotebook(
            lastUsedNotebook, errorMessage),
        "Found some last used notebook which shouldn't have been found: "
            << lastUsedNotebook);

    // Check FindDefaultOrLastUsedNotebook
    Notebook defaultOrLastUsedNotebook;

    QVERIFY2(
        localStorageManager.findDefaultOrLastUsedNotebook(
            defaultOrLastUsedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        defaultOrLastUsedNotebook == defaultNotebook,
        "Found defaultOrLastUsed notebook which "
            << "should be the same as default notebook "
            << "right now but it is not. "
            << "Default notebook: " << defaultNotebook
            << ", defaultOrLastUsedNotebook: " << defaultOrLastUsedNotebook);

    // Check Update + Find
    Notebook modifiedNotebook(notebook);

    modifiedNotebook.setUpdateSequenceNumber(
        notebook.updateSequenceNumber() + 1);

    modifiedNotebook.setLinkedNotebookGuid(QLatin1String(""));
    modifiedNotebook.setName(notebook.name() + QStringLiteral("_modified"));
    modifiedNotebook.setDefaultNotebook(false);
    modifiedNotebook.setLastUsed(true);

    modifiedNotebook.setModificationTimestamp(
        notebook.modificationTimestamp() + 1);

    modifiedNotebook.setPublishingUri(
        notebook.publishingUri() + QStringLiteral("_modified"));

    modifiedNotebook.setPublishingAscending(!notebook.isPublishingAscending());

    modifiedNotebook.setPublishingPublicDescription(
        notebook.publishingPublicDescription() + QStringLiteral("_modified"));

    modifiedNotebook.setStack(notebook.stack() + QStringLiteral("_modified"));

    modifiedNotebook.setBusinessNotebookDescription(
        notebook.businessNotebookDescription() + QStringLiteral("_modified"));

    modifiedNotebook.setBusinessNotebookRecommended(
        !notebook.isBusinessNotebookRecommended());

    modifiedNotebook.setCanExpungeNotes(false);
    modifiedNotebook.setCanEmailNotes(false);
    modifiedNotebook.setCanPublishToPublic(false);
    modifiedNotebook.setFavorited(true);

    QVERIFY2(
        localStorageManager.updateNotebook(modifiedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNotebook = Notebook();
    foundNotebook.setGuid(modifiedNotebook.guid());
    if (modifiedNotebook.hasLinkedNotebookGuid()) {
        foundNotebook.setLinkedNotebookGuid(
            modifiedNotebook.linkedNotebookGuid());
    }

    QVERIFY2(
        localStorageManager.findNotebook(foundNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        modifiedNotebook == foundNotebook,
        "Updated and found notebooks in the local storage "
            << "don't match: notebook updated in the local storage: "
            << modifiedNotebook
            << "\nNotebook found in the local storage: " << foundNotebook);

    // Check FindDefaultNotebook (failure expected)
    defaultNotebook = Notebook();

    VERIFY2(
        !localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage),
        "Found some default notebook which shouldn't have been found: "
            << defaultNotebook);

    // Check FindLastUsedNotebook
    lastUsedNotebook = Notebook();

    QVERIFY2(
        localStorageManager.findLastUsedNotebook(
            lastUsedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check FindDefaultOrLastUsedNotebook
    defaultOrLastUsedNotebook = Notebook();

    QVERIFY2(
        localStorageManager.findDefaultOrLastUsedNotebook(
            defaultOrLastUsedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        defaultOrLastUsedNotebook == lastUsedNotebook,
        "Found defaultOrLastUsed notebook which "
            << "should be the same as last used notebook "
            << "right now but it is not. "
            << "Last used notebook: " << lastUsedNotebook
            << "\nDefaultOrLastUsedNotebook: " << defaultOrLastUsedNotebook);

    // Check notebookCount to return 1
    int count = localStorageManager.notebookCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(
            QString::fromUtf8("notebookCount returned result %1 different "
                              "from the expected one (1)")
                .arg(count)));

    // Check Expunge + Find (failure expected)
    QVERIFY2(
        localStorageManager.expungeNotebook(modifiedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !localStorageManager.findNotebook(foundNotebook, errorMessage),
        "Error: found Notebook which should have "
            << "been expunged from the local storage: "
            << "Notebook expunged from the local storage: " << modifiedNotebook
            << "\nNotebook found in the local storage: " << foundNotebook);

    // Check notebookCount to return 0
    count = localStorageManager.notebookCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 0,
        qPrintable(
            QString::fromUtf8("notebookCount returned result %1 different "
                              "from the expected one (0)")
                .arg(count)));
}

void TestFindNotebookByNameWithDiacritics()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("TestFindNotebookByNameWithDiacriticsFakeUser"),
        Account::Type::Local);

    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook1;
    notebook1.setGuid(UidGenerator::Generate());
    notebook1.setUpdateSequenceNumber(1);
    notebook1.setName(QStringLiteral("notebook"));
    notebook1.setDefaultNotebook(false);
    notebook1.setLastUsed(false);

    Notebook notebook2;
    notebook2.setGuid(UidGenerator::Generate());
    notebook2.setUpdateSequenceNumber(2);
    notebook2.setName(QStringLiteral("notébook"));
    notebook2.setDefaultNotebook(false);
    notebook2.setLastUsed(false);

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addNotebook(notebook2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Notebook notebookToFind;
    notebookToFind.unsetLocalUid();
    notebookToFind.setName(notebook1.name());

    QVERIFY2(
        localStorageManager.findNotebook(notebookToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        notebookToFind == notebook1,
        "Found wrong notebook by name: expected notebook: "
            << notebook1 << "\nActually found notebook: " << notebookToFind);

    notebookToFind = Notebook();
    notebookToFind.unsetLocalUid();
    notebookToFind.setName(notebook2.name());

    QVERIFY2(
        localStorageManager.findNotebook(notebookToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        notebookToFind == notebook2,
        "Found wrong notebook by name: expected notebook: "
            << notebook2 << "\nActually found notebook: " << notebookToFind);
}

void TestUserAddFindUpdateDeleteExpungeInLocalStorage()
{
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
    LocalStorageManager localStorageManager(account, startupOptions);

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

    userAttributes.defaultLocationName =
        QStringLiteral("fake_default_location_name");

    userAttributes.defaultLatitude = 1.0;
    userAttributes.defaultLongitude = 2.0;
    userAttributes.preactivation = false;
    QList<QString> viewedPromotions;
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 1"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 2"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 3"));
    userAttributes.viewedPromotions = viewedPromotions;

    userAttributes.incomingEmailAddress =
        QStringLiteral("fake_incoming_email_address");

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

    userAttributes.referralProof =
        QStringLiteral("I_have_no_idea_what_this_means");

    userAttributes.educationalDiscount = false;
    userAttributes.businessAddress = QStringLiteral("fake_business_address");
    userAttributes.hideSponsorBilling = true;
    userAttributes.useEmailAutoFiling = true;

    userAttributes.reminderEmailConfig =
        qevercloud::ReminderEmailConfig::DO_NOT_SEND;

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

    accounting.premiumCommerceService =
        QStringLiteral("Fake premium commerce service");

    accounting.premiumServiceStart = 8;

    accounting.premiumServiceSKU =
        QStringLiteral("Fake code associated with the purchase");

    accounting.lastSuccessfulCharge = 7;
    accounting.lastFailedCharge = 5;
    accounting.lastFailedChargeReason = QStringLiteral("No money, no honey");
    accounting.nextPaymentDue = 12;
    accounting.premiumLockUntil = 11;
    accounting.updated = 10;

    accounting.premiumSubscriptionNumber =
        QStringLiteral("Fake premium subscription number");

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

    QVERIFY2(
        user.checkParameters(errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addUser(user, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const qint32 initialUserId = user.id();
    User foundUser;
    foundUser.setId(initialUserId);

    QVERIFY2(
        localStorageManager.findUser(foundUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        user == foundUser,
        "Added and found users in the local storage don't "
            << "match: user added to the local storage: " << user
            << "\nIUser found in the local storage: " << foundUser);

    // Check Update + Find
    User modifiedUser;
    modifiedUser.setId(user.id());
    modifiedUser.setUsername(user.username() + QStringLiteral("_modified"));
    modifiedUser.setEmail(user.email() + QStringLiteral("_modified"));
    modifiedUser.setName(user.name() + QStringLiteral("_modified"));
    modifiedUser.setTimezone(user.timezone() + QStringLiteral("_modified"));
    modifiedUser.setPrivilegeLevel(static_cast<qint8>(user.privilegeLevel()));
    modifiedUser.setCreationTimestamp(user.creationTimestamp());
    modifiedUser.setModificationTimestamp(user.modificationTimestamp() + 1);
    modifiedUser.setActive(true);

    qevercloud::UserAttributes modifiedUserAttributes;
    modifiedUserAttributes = user.userAttributes();

    modifiedUserAttributes.defaultLocationName->append(
        QStringLiteral("_modified"));

    modifiedUserAttributes.comments->append(QStringLiteral("_modified"));

    modifiedUserAttributes.preferredCountry->append(
        QStringLiteral("_modified"));

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

    modifiedAccounting.premiumSubscriptionNumber->append(
        QStringLiteral("_modified"));

    modifiedAccounting.updated += 1;

    modifiedUser.setAccounting(std::move(modifiedAccounting));

    qevercloud::AccountLimits modifiedAccountLimits;
    modifiedAccountLimits = user.accountLimits();
    modifiedAccountLimits.noteTagCountMax = 2;
    modifiedAccountLimits.userLinkedNotebookMax = 2;
    modifiedAccountLimits.userNotebookCountMax = 2;

    modifiedUser.setAccountLimits(std::move(modifiedAccountLimits));

    QVERIFY2(
        localStorageManager.updateUser(modifiedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundUser.clear();
    foundUser.setId(modifiedUser.id());

    QVERIFY2(
        localStorageManager.findUser(foundUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        modifiedUser == foundUser,
        "Updated and found users in the local storage don't match: "
            << "User updated in the local storage: " << modifiedUser
            << "\nIUser found in the local storage: " << foundUser);

    // Check userCount to return 1
    int count = localStorageManager.userCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("userCount returned value %1 different "
                                     "from the expected one (1)")
                       .arg(count)));

    // Check Delete + Find
    modifiedUser.setDeletionTimestamp(5);

    QVERIFY2(
        localStorageManager.deleteUser(modifiedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundUser.clear();
    foundUser.setId(modifiedUser.id());

    QVERIFY2(
        localStorageManager.findUser(foundUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        modifiedUser == foundUser,
        "Deleted and found users in the local storage don't match"
            << ": User marked deleted in the local storage: " << modifiedUser
            << "\nIUser found in the local storage: " << foundUser);

    // Check userCount to return 0 (as it doesn't account for deleted resources)
    count = localStorageManager.userCount(errorMessage);
    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 0,
        qPrintable(QString::fromUtf8("userCount returned value %1 different "
                                     "from the expected one (0)")
                       .arg(count)));

    // Check Expunge + Find (failure expected)
    QVERIFY2(
        localStorageManager.expungeUser(modifiedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundUser.clear();
    foundUser.setId(modifiedUser.id());

    VERIFY2(
        !localStorageManager.findUser(foundUser, errorMessage),
        "Error: found User which should have "
        "been expunged from the local storage: "
            << "User expunged from the local storage: " << modifiedUser
            << "\nIUser found in the local storage: " << foundUser);
}

void TestSequentialUpdatesInLocalStorage()
{
    // 1) Create LocalStorageManager

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("LocalStorageManagerSequentialUpdatesTestFakeUser"),
        Account::Type::Evernote, 0);

    LocalStorageManager localStorageManager(account, startupOptions);

    // 2) Create User
    User user;
    user.setId(1);
    user.setUsername(QStringLiteral("checker"));
    user.setEmail(QStringLiteral("mail@checker.com"));
    user.setTimezone(QStringLiteral("Europe/Moscow"));

    user.setPrivilegeLevel(
        static_cast<qint8>(qevercloud::PrivilegeLevel::NORMAL));

    user.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    user.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());
    user.setActive(true);

    qevercloud::UserAttributes userAttributes;
    userAttributes.defaultLocationName = QStringLiteral("Default location");
    userAttributes.comments = QStringLiteral("My comment");
    userAttributes.preferredLanguage = QStringLiteral("English");

    userAttributes.viewedPromotions = QStringList();

    userAttributes.viewedPromotions.ref()
        << QStringLiteral("Promotion #1") << QStringLiteral("Promotion #2")
        << QStringLiteral("Promotion #3");

    userAttributes.recentMailedAddresses = QStringList();

    userAttributes.recentMailedAddresses.ref()
        << QStringLiteral("Recent mailed address #1")
        << QStringLiteral("Recent mailed address #2")
        << QStringLiteral("Recent mailed address #3");

    user.setUserAttributes(std::move(userAttributes));

    qevercloud::Accounting accounting;
    accounting.premiumOrderNumber = QStringLiteral("Premium order number");

    accounting.premiumSubscriptionNumber =
        QStringLiteral("Premium subscription number");

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

    // 3) Add user to local storage
    QVERIFY2(
        localStorageManager.addUser(user, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 4) Create new user without all the supplementary data but with the same
    // id and update it in local storage
    User updatedUser;
    updatedUser.setId(1);
    updatedUser.setUsername(QStringLiteral("checker"));
    updatedUser.setEmail(QStringLiteral("mail@checker.com"));

    updatedUser.setPrivilegeLevel(
        static_cast<qint8>(qevercloud::PrivilegeLevel::NORMAL));

    updatedUser.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setActive(true);

    QVERIFY2(
        localStorageManager.updateUser(updatedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 5) Find this user in local storage, check whether it has user attributes,
    // accounting, business user info and premium info (it shouldn't)
    User foundUser;
    foundUser.setId(1);

    QVERIFY2(
        localStorageManager.findUser(foundUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundUser.hasUserAttributes(),
        "Updated user found the in the local storage "
            << "still has user attributes while it "
            << "shouldn't have them after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.hasAccounting(),
        "Updated user found in the local storage "
            << "still has accounting while it shouldn't "
            << "have it after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.hasBusinessUserInfo(),
        "Updated user found in the local storage "
            << "still has business user info "
            << "while it shouldn't have it after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.hasAccountLimits(),
        "Updated user found in the local storage "
            << "still has account limits while it "
            << "shouldn't have them after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    // 6) Create Notebook with restrictions and shared notebooks
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

    notebook.setPublishingPublicDescription(
        QStringLiteral("Fake public description"));

    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    notebook.setBusinessNotebookDescription(
        QStringLiteral("Fake business notebook description"));

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

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilegeLevel(1);
    sharedNotebook.setReminderNotifyEmail(true);
    sharedNotebook.setReminderNotifyApp(false);

    notebook.addSharedNotebook(sharedNotebook);

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 7) Update notebook: remove restrictions and shared notebooks
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

    updatedNotebook.setPublishingPublicDescription(
        QStringLiteral("Fake public description"));

    updatedNotebook.setPublished(true);
    updatedNotebook.setStack(QStringLiteral("Fake notebook stack"));

    updatedNotebook.setBusinessNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    updatedNotebook.setBusinessNotebookPrivilegeLevel(1);
    updatedNotebook.setBusinessNotebookRecommended(true);

    QVERIFY2(
        localStorageManager.updateNotebook(updatedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 8) Find notebook, ensure it doesn't have neither restrictions
    // nor shared notebooks

    Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());

    QVERIFY2(
        localStorageManager.findNotebook(foundNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundNotebook.hasSharedNotebooks(),
        "Updated notebook found in the local "
            << "storage has shared notebooks "
            << "while it shouldn't have them"
            << ", original notebook: " << notebook << "\nUpdated notebook: "
            << updatedNotebook << "\nFound notebook: " << foundNotebook);

    VERIFY2(
        !foundNotebook.hasRestrictions(),
        "Updated notebook found in the local "
            << "storage has restrictions "
            << "while it shouldn't have them"
            << ", original notebook: " << notebook << "\nUpdated notebook: "
            << updatedNotebook << "\nFound notebook: " << foundNotebook);

    // 9) Create tag
    Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setUpdateSequenceNumber(1);
    tag.setName(QStringLiteral("Fake tag name"));

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 10) Create note, add this tag to it along with some resource
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

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 11) Update note, remove tag guid and resource
    Note updatedNote;
    updatedNote.setLocalUid(note.localUid());
    updatedNote.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
    updatedNote.setUpdateSequenceNumber(1);
    updatedNote.setTitle(QStringLiteral("Fake note title"));

    updatedNote.setContent(
        QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    updatedNote.setCreationTimestamp(1);
    updatedNote.setModificationTimestamp(1);
    updatedNote.setActive(true);
    updatedNote.setNotebookGuid(notebook.guid());
    updatedNote.setNotebookLocalUid(notebook.localUid());

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(
            updatedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 12) Find updated note in local storage, ensure it doesn't have
    // neither tag guids, nor resources
    Note foundNote;
    foundNote.setLocalUid(updatedNote.localUid());
    foundNote.setGuid(updatedNote.guid());

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundNote.hasTagGuids(),
        "Updated note found in local storage "
            << "has tag guids while it shouldn't have them"
            << ", original note: " << note << "\nUpdated note: " << updatedNote
            << "\nFound note: " << foundNote);

    VERIFY2(
        !foundNote.hasResources(),
        "Updated note found in local storage "
            << "has resources while it shouldn't have them"
            << ", original note: " << note << "\nUpdated note: " << updatedNote
            << "\nFound note: " << foundNote);

    // 13) Add resource attributes to the resource and add resource to note
    auto & resourceAttributes = resource.resourceAttributes();
    resourceAttributes.applicationData = qevercloud::LazyMap();
    resourceAttributes.applicationData->keysOnly = QSet<QString>();
    resourceAttributes.applicationData->fullMap = QMap<QString, QString>();

    resourceAttributes.applicationData->keysOnly.ref()
        << QStringLiteral("key_1") << QStringLiteral("key_2")
        << QStringLiteral("key_3");

    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_1")] =
        QStringLiteral("value_1");

    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_2")] =
        QStringLiteral("value_2");

    resourceAttributes.applicationData->fullMap.ref()[QStringLiteral("key_3")] =
        QStringLiteral("value_3");

    updatedNote.addResource(resource);

    QVERIFY2(
        localStorageManager.updateNote(
            updatedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 14) Remove resource attributes from note's resource and update it again
    QList<Resource> resources = updatedNote.resources();

    VERIFY2(
        !resources.empty(),
        "Note returned empty list of resources "
            << "while it should have contained at least "
            << "one entry, updated note: " << updatedNote);

    Resource & updatedResource = resources[0];
    auto & underlyngResourceAttributes = updatedResource.resourceAttributes();
    underlyngResourceAttributes = qevercloud::ResourceAttributes();

    updatedNote.setResources(resources);

    QVERIFY2(
        localStorageManager.updateNote(
            updatedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 15) Find note in the local storage again
    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    resources = foundNote.resources();

    VERIFY2(
        !resources.empty(),
        "Note returned empty list of resources "
            << "while it should have contained "
            << "at least one entry, found note: " << foundNote);

    Resource & foundResource = resources[0];
    auto & foundResourceAttributes = foundResource.resourceAttributes();

    VERIFY2(
        !foundResourceAttributes.applicationData.isSet(),
        "Resource from updated note has application "
            << "data while it shouldn't have it, found resource: "
            << foundResource);
}

void TestAccountHighUsnInLocalStorage()
{
    // 1) Create LocalStorageManager

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("LocalStorageManagerAccountHighUsnTestFakeUser"),
        Account::Type::Evernote, 0);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Verify that account high USN is initially zero (since all tables
    // are empty)

    qint32 initialUsn = localStorageManager.accountHighUsn({}, errorMessage);
    QVERIFY2(initialUsn == 0, qPrintable(errorMessage.nonLocalizedString()));
    qint32 currentUsn = initialUsn;

    // 3) Create some user's own notebooks with different USNs

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

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(firstNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(secondNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(thirdNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 4) Verify the current value of the account high USN

    errorMessage.clear();
    qint32 accountHighUsn =
        localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == thirdNotebook.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdNotebook.updateSequenceNumber()) << ", got "
            << QString::number(accountHighUsn));

    // 5) Create some user's own tags with different USNs

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

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(firstTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(secondTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(thirdTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 6) Verify the current value of the account high USN

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == thirdTag.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdTag.updateSequenceNumber()) << ", got "
            << QString::number(accountHighUsn));

    // 7) Create some user's own notes with different USNs

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

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 8) Verify the current value of the account high USN

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == secondNote.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(secondNote.updateSequenceNumber()) << ", got "
            << QString::number(accountHighUsn));

    // 9) Create one more note, this time with a resource which USN
    // is higher than the note's one

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

    thirdNoteResource.setDataBody(
        QByteArray::fromStdString(std::string("Something")));

    thirdNoteResource.setDataSize(thirdNoteResource.dataBody().size());

    thirdNoteResource.setDataHash(QCryptographicHash::hash(
        thirdNoteResource.dataBody(), QCryptographicHash::Md5));

    thirdNoteResource.setMime(QStringLiteral("text/plain"));
    thirdNoteResource.setUpdateSequenceNumber(currentUsn++);

    thirdNote.addResource(thirdNoteResource);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(thirdNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 10) Verify the current value of the account high USN

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == thirdNoteResource.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdNoteResource.updateSequenceNumber())
            << ", got " << QString::number(accountHighUsn));

    // 11) Create some user's own saved sarches with different USNs

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

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addSavedSearch(firstSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addSavedSearch(secondSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addSavedSearch(thirdSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 12) Verify the current value of the account high USN

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == thirdSearch.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdSearch.updateSequenceNumber()) << ", got "
            << QString::number(accountHighUsn));

    // 13) Create a linked notebook

    LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(UidGenerator::Generate());
    linkedNotebook.setUpdateSequenceNumber(currentUsn++);
    linkedNotebook.setShareName(QStringLiteral("Share name"));
    linkedNotebook.setUsername(QStringLiteral("Username"));
    linkedNotebook.setShardId(UidGenerator::Generate());
    linkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    linkedNotebook.setUri(UidGenerator::Generate());

    errorMessage.clear();
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 14) Verify the current value of the account high USN

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == linkedNotebook.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(linkedNotebook.updateSequenceNumber())
            << ", got " << QString::number(accountHighUsn));

    // 15) Add notebook and some tags and notes corresponding to the linked
    // notebook

    Notebook notebookFromLinkedNotebook;
    notebookFromLinkedNotebook.setGuid(linkedNotebook.sharedNotebookGlobalId());
    notebookFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    notebookFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    notebookFromLinkedNotebook.setName(
        QStringLiteral("Notebook from linked notebook"));

    notebookFromLinkedNotebook.setCreationTimestamp(
        QDateTime::currentMSecsSinceEpoch());

    notebookFromLinkedNotebook.setModificationTimestamp(
        notebookFromLinkedNotebook.creationTimestamp());

    Tag firstTagFromLinkedNotebook;
    firstTagFromLinkedNotebook.setGuid(UidGenerator::Generate());

    firstTagFromLinkedNotebook.setName(
        QStringLiteral("First tag from linked notebook"));

    firstTagFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    firstTagFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    Tag secondTagFromLinkedNotebook;
    secondTagFromLinkedNotebook.setGuid(UidGenerator::Generate());

    secondTagFromLinkedNotebook.setName(
        QStringLiteral("Second tag from linked notebook"));

    secondTagFromLinkedNotebook.setLinkedNotebookGuid(linkedNotebook.guid());
    secondTagFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    Note firstNoteFromLinkedNotebook;
    firstNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    firstNoteFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    firstNoteFromLinkedNotebook.setNotebookGuid(
        notebookFromLinkedNotebook.guid());

    firstNoteFromLinkedNotebook.setNotebookLocalUid(
        notebookFromLinkedNotebook.localUid());

    firstNoteFromLinkedNotebook.setTitle(
        QStringLiteral("First note from linked notebook"));

    firstNoteFromLinkedNotebook.setCreationTimestamp(
        QDateTime::currentMSecsSinceEpoch());

    firstNoteFromLinkedNotebook.setModificationTimestamp(
        firstNoteFromLinkedNotebook.creationTimestamp());

    firstNoteFromLinkedNotebook.addTagLocalUid(
        firstTagFromLinkedNotebook.localUid());

    firstNoteFromLinkedNotebook.addTagGuid(firstTagFromLinkedNotebook.guid());

    Note secondNoteFromLinkedNotebook;
    secondNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    secondNoteFromLinkedNotebook.setUpdateSequenceNumber(currentUsn++);

    secondNoteFromLinkedNotebook.setNotebookGuid(
        notebookFromLinkedNotebook.guid());

    secondNoteFromLinkedNotebook.setNotebookLocalUid(
        notebookFromLinkedNotebook.localUid());

    secondNoteFromLinkedNotebook.setTitle(
        QStringLiteral("Second note from linked notebook"));

    secondNoteFromLinkedNotebook.setCreationTimestamp(
        QDateTime::currentMSecsSinceEpoch());

    secondNoteFromLinkedNotebook.setModificationTimestamp(
        secondNoteFromLinkedNotebook.creationTimestamp());

    Resource secondNoteFromLinkedNotebookResource;
    secondNoteFromLinkedNotebookResource.setGuid(UidGenerator::Generate());

    secondNoteFromLinkedNotebookResource.setNoteGuid(
        secondNoteFromLinkedNotebook.guid());

    secondNoteFromLinkedNotebookResource.setNoteLocalUid(
        secondNoteFromLinkedNotebook.localUid());

    secondNoteFromLinkedNotebookResource.setDataBody(
        QByteArray::fromStdString(std::string("Other something")));

    secondNoteFromLinkedNotebookResource.setDataSize(
        secondNoteFromLinkedNotebookResource.dataBody().size());

    secondNoteFromLinkedNotebookResource.setDataHash(QCryptographicHash::hash(
        secondNoteFromLinkedNotebookResource.dataBody(),
        QCryptographicHash::Md5));

    secondNoteFromLinkedNotebookResource.setUpdateSequenceNumber(currentUsn++);

    secondNoteFromLinkedNotebook.addResource(
        secondNoteFromLinkedNotebookResource);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(
            notebookFromLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(firstTagFromLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(secondTagFromLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNoteFromLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNoteFromLinkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 16) Verify the current value of the account high USN for user's own
    // stuff

    errorMessage.clear();
    accountHighUsn = localStorageManager.accountHighUsn({}, errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn == linkedNotebook.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(linkedNotebook.updateSequenceNumber())
            << ", got " << QString::number(accountHighUsn));

    // 17) Verify the current value of the account high USN for the linked
    // notebook

    errorMessage.clear();

    accountHighUsn =
        localStorageManager.accountHighUsn(linkedNotebook.guid(), errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn ==
            secondNoteFromLinkedNotebookResource.updateSequenceNumber(),
        "Wrong value of account high USN, expected "
            << QString::number(
                   secondNoteFromLinkedNotebookResource.updateSequenceNumber())
            << ", got " << QString::number(accountHighUsn));
}

void TestAddingNoteWithoutLocalUid()
{
    // 1) Create LocalStorageManager

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"),
        Account::Type::Evernote, 0);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Add a notebook in order to test adding notes

    Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 3) Try to add a note without local uid without tags or resources
    Note firstNote;
    firstNote.unsetLocalUid();
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        !firstNote.localUid().isEmpty(),
        qPrintable(QStringLiteral(
            "Note local uid is empty after LocalStorageManager::addNote method "
            "returning")));

    // 4) Add some tags in order to test adding notes with tags
    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(firstTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(secondTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(thirdTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 5) Try to add a note without local uid with tag guids
    Note secondNote;
    secondNote.unsetLocalUid();
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));
    secondNote.addTagGuid(firstTag.guid());
    secondNote.addTagGuid(secondTag.guid());
    secondNote.addTagGuid(thirdTag.guid());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 6) Try to add a note without local uid with tag guids and with resources
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
    QByteArray dataBody = QByteArray::fromStdString(std::string("Data"));
    resource.setDataBody(dataBody);
    resource.setDataSize(dataBody.size());

    resource.setDataHash(
        QCryptographicHash::hash(dataBody, QCryptographicHash::Md5));

    resource.setMime(QStringLiteral("text/plain"));

    thirdNote.addResource(resource);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(thirdNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));
}

void TestNoteTagIdsComplementWhenAddingAndUpdatingNote()
{
    // 1) Create LocalStorageManager

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    Account account(
        QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"),
        Account::Type::Evernote, 0);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Add a notebook in order to test adding notes

    Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 3) Add some tags
    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(firstTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(secondTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(thirdTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 4) Add a note without tag local uids but with tag guids
    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    firstNote.addTagGuid(firstTag.guid());
    firstNote.addTagGuid(secondTag.guid());
    firstNote.addTagGuid(thirdTag.guid());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        firstNote.hasTagLocalUids(),
        qPrintable(QStringLiteral("Note has no tag local uids after "
                                  "LocalStorageManager::addNote method "
                                  "returning")));

    const QStringList & tagLocalUids = firstNote.tagLocalUids();

    QVERIFY2(
        tagLocalUids.size() == 3,
        qPrintable(QStringLiteral(
            "Note's tag local uids have improper size not matching the number "
            "of tag guids after LocalStorageManager::addNote method "
            "returning")));

    QVERIFY2(
        tagLocalUids.contains(firstTag.localUid()) &&
            tagLocalUids.contains(secondTag.localUid()) &&
            tagLocalUids.contains(thirdTag.localUid()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag local uids it should have after "
            "LocalStorageManager::addNote method returning")));

    // 5) Add a note without tag guids but with tag local uids
    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));

    secondNote.addTagLocalUid(firstTag.localUid());
    secondNote.addTagLocalUid(secondTag.localUid());
    secondNote.addTagLocalUid(thirdTag.localUid());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        secondNote.hasTagGuids(),
        qPrintable(QStringLiteral(
            "Note has no tag guids after LocalStorageManager::addNote method "
            "returning")));

    const QStringList & tagGuids = secondNote.tagGuids();

    QVERIFY2(
        tagGuids.size() == 3,
        qPrintable(QStringLiteral(
            "Note's tag guids have improper size not matching the number of "
            "tag local uids after LocalStorageManager::addNote method "
            "returning")));

    QVERIFY2(
        tagGuids.contains(firstTag.guid()) &&
            tagGuids.contains(secondTag.guid()) &&
            tagGuids.contains(thirdTag.guid()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag guids it should have after "
            "LocalStorageManager::addNote method returning")));

    // 6) Update note with tag guids
    firstNote.setTitle(QStringLiteral("Updated first note"));
    firstNote.setTagLocalUids(QStringList());
    firstNote.setTagGuids(QStringList() << firstTag.guid() << secondTag.guid());

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(
            firstNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        firstNote.hasTagLocalUids(),
        qPrintable(QStringLiteral(
            "Note has no tag local uids after LocalStorageManager::updateNote "
            "method returning")));

    const QStringList & updatedTagLocalUids = firstNote.tagLocalUids();

    QVERIFY2(
        updatedTagLocalUids.size() == 2,
        qPrintable(QStringLiteral(
            "Note's tag local uids have improper size not matching the number "
            "of tag guids after LocalStorageManager::updateNote method "
            "returning")));

    QVERIFY2(
        updatedTagLocalUids.contains(firstTag.localUid()) &&
            updatedTagLocalUids.contains(secondTag.localUid()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag local uids it should have after "
            "LocalStorageManager::updateNote method returning")));

    // 7) Update note with tag guids
    secondNote.setTitle(QStringLiteral("Updated second note"));
    secondNote.setTagGuids(QStringList());

    secondNote.setTagLocalUids(
        QStringList() << firstTag.localUid() << secondTag.localUid());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.updateNote(
            secondNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        secondNote.hasTagGuids(),
        qPrintable(QStringLiteral(
            "Note has no tag guids after LocalStorageManager::updateNote "
            "method returning")));

    const QStringList & updatedTagGuids = secondNote.tagGuids();

    QVERIFY2(
        updatedTagGuids.size() == 2,
        qPrintable(QStringLiteral(
            "Note's tag guids have improper size not matching the number of "
            "tag local uids after LocalStorageManager::updateNote method "
            "returning")));

    QVERIFY2(
        updatedTagGuids.contains(firstTag.guid()) &&
            updatedTagGuids.contains(secondTag.guid()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag guids it should have after "
            "LocalStorageManager::updateNote method returning")));
}

} // namespace test
} // namespace quentier
