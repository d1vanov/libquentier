/*
 * Copyright 2016-2021 Dmitry Ivanov
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
#include <quentier/types/NoteUtils.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/generated/Types.h>

#include <QCryptographicHash>
#include <QtTest/QtTest>

namespace quentier {
namespace test {

void TestSavedSearchAddFindUpdateExpungeInLocalStorage()
{
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("CoreTesterFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::SavedSearch search;
    search.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    search.setUpdateSequenceNum(1);
    search.setName(QStringLiteral("Fake saved search name"));
    search.setQuery(QStringLiteral("Fake saved search query"));
    search.setFormat(qevercloud::QueryFormat::USER);

    qevercloud::SavedSearchScope scope;
    scope.setIncludeAccount(true);
    scope.setIncludeBusinessLinkedNotebooks(true);
    scope.setIncludePersonalLinkedNotebooks(true);

    ErrorString errorMessage;

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addSavedSearch(search, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString searchLocalId = search.localId();
    qevercloud::SavedSearch foundSearch;
    foundSearch.setLocalId(searchLocalId);

    QVERIFY2(
        localStorageManager.findSavedSearch(foundSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        search == foundSearch,
        "Added and found saved searches in the local storage don't match: "
            << "saved search added to the local storage: " << search
            << "\nSaved search found in the local storage:" << foundSearch);

    // Check Find by name
    qevercloud::SavedSearch foundByNameSearch;
    foundByNameSearch.setLocalId(QString{});
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
    qevercloud::SavedSearch modifiedSearch = search;
    modifiedSearch.setUpdateSequenceNum(search.updateSequenceNum().value() + 1);
    modifiedSearch.setName(search.name().value() + QStringLiteral("_modified"));
    modifiedSearch.setQuery(search.query().value() + QStringLiteral("_modified"));
    modifiedSearch.setLocallyFavorited(true);
    modifiedSearch.setLocallyModified(true);

    QVERIFY2(
        localStorageManager.updateSavedSearch(modifiedSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.findSavedSearch(foundSearch, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    modifiedSearch.setLocalId(searchLocalId);
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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("TestFindSavedSearchByNameWithDiacriticsFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::SavedSearch search1;
    search1.setGuid(UidGenerator::Generate());
    search1.setUpdateSequenceNum(1);
    search1.setName(QStringLiteral("search"));

    qevercloud::SavedSearch search2;
    search2.setGuid(UidGenerator::Generate());
    search2.setUpdateSequenceNum(2);
    search2.setName(QStringLiteral("séarch"));

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addSavedSearch(search1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addSavedSearch(search2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::SavedSearch searchToFind;
    searchToFind.setLocalId(QString{});
    searchToFind.setName(search1.name());

    QVERIFY2(
        localStorageManager.findSavedSearch(searchToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        searchToFind == search1,
        "Found wrong saved search by name: expected saved search: "
            << search1 << "\nActually found search: " << searchToFind);

    searchToFind = qevercloud::SavedSearch();
    searchToFind.setLocalId(QString{});
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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("CoreTesterFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000046"));

    linkedNotebook.setUpdateSequenceNum(1);

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

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString linkedNotebookGuid = linkedNotebook.guid().value();

    qevercloud::LinkedNotebook foundLinkedNotebook;
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
    qevercloud::LinkedNotebook modifiedLinkedNotebook = linkedNotebook;

    modifiedLinkedNotebook.setUpdateSequenceNum(
        linkedNotebook.updateSequenceNum().value() + 1);

    modifiedLinkedNotebook.setShareName(
        linkedNotebook.shareName().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setUsername(
        linkedNotebook.username().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setShardId(
        linkedNotebook.shardId().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setSharedNotebookGlobalId(
        linkedNotebook.sharedNotebookGlobalId().value() +
        QStringLiteral("_modified"));

    modifiedLinkedNotebook.setUri(
        linkedNotebook.uri().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setNoteStoreUrl(
        linkedNotebook.noteStoreUrl().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setWebApiUrlPrefix(
        linkedNotebook.webApiUrlPrefix().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setStack(
        linkedNotebook.stack().value() + QStringLiteral("_modified"));

    modifiedLinkedNotebook.setBusinessId(
        linkedNotebook.businessId().value() + 1);

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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("CoreTesterFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNum(1);
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

    qevercloud::Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setLinkedNotebookGuid(linkedNotebook.guid().value());
    tag.setUpdateSequenceNum(1);
    tag.setName(QStringLiteral("Fake tag name"));

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString tagLocalId = tag.localId();

    qevercloud::Tag foundTag;
    foundTag.setLocalId(tagLocalId);

    const auto tagLinkedNotebookGuid = tag.linkedNotebookGuid();
    foundTag.setLinkedNotebookGuid(tagLinkedNotebookGuid);

    QVERIFY2(
        localStorageManager.findTag(foundTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tag == foundTag,
        "Added and found tags in the local storage don't match: "
            << "tag added to the local storage: " << tag
            << "\nTag found in the local storage: " << foundTag);

    // Check Find by name
    qevercloud::Tag foundByNameTag;
    foundByNameTag.setLocalId(QString{});
    foundByNameTag.setName(tag.name());
    foundByNameTag.setLinkedNotebookGuid(tagLinkedNotebookGuid);

    QVERIFY2(
        localStorageManager.findTag(foundByNameTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tag == foundByNameTag,
        "Tag found by name in the local storage doesn't match "
            << "the original tag: tag found by name: " << foundByNameTag
            << "\nOriginal tag: " << tag);

    // Check Update + Find
    qevercloud::Tag modifiedTag = tag;
    modifiedTag.setUpdateSequenceNum(tag.updateSequenceNum().value() + 1);
    modifiedTag.setLinkedNotebookGuid(QString{});
    modifiedTag.setName(tag.name().value() + QStringLiteral("_modified"));
    modifiedTag.setLocallyFavorited(true);
    modifiedTag.setLocalId(QString{});

    QVERIFY2(
        localStorageManager.updateTag(modifiedTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const auto modifiedTagLinkedNotebookGuid =
        modifiedTag.linkedNotebookGuid();

    if (!modifiedTagLinkedNotebookGuid) {
        foundTag.setLinkedNotebookGuid(std::nullopt);
    }

    QVERIFY2(
        localStorageManager.findTag(foundTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    modifiedTag.setLocalId(tagLocalId);

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
    qevercloud::Tag newTag;
    newTag.setName(QStringLiteral("New tag"));
    newTag.setParentGuid(tag.guid());
    newTag.setParentTagLocalId(tag.localId());

    QVERIFY2(
        localStorageManager.addTag(newTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag foundNewTag;
    foundNewTag.setLocalId(newTag.localId());

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
    QStringList expungedChildTagLocalIds;

    QVERIFY2(
        localStorageManager.expungeTag(
            modifiedTag, expungedChildTagLocalIds, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    Q_UNUSED(expungedChildTagLocalIds)

    VERIFY2(
        !localStorageManager.findTag(foundTag, errorMessage),
        "Error: found tag which should have "
            << "been expunged from the local storage"
            << ": Tag expunged from the local storage: " << modifiedTag
            << "\nTag found in the local storage: " << foundTag);
}

void TestFindTagByNameWithDiacritics()
{
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("TestFindTagByNameWithDiacriticsFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNum(1);
    tag1.setName(QStringLiteral("tag"));

    qevercloud::Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNum(2);
    tag2.setName(QStringLiteral("tāg"));

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addTag(tag1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addTag(tag2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag tagToFind;
    tagToFind.setLocalId(QString{});
    tagToFind.setName(tag1.name());

    QVERIFY2(
        localStorageManager.findTag(tagToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        tagToFind == tag1,
        "Found wrong tag by name: expected tag: "
            << tag1 << "\nActually found tag: " << tagToFind);

    tagToFind = qevercloud::Tag();
    tagToFind.setLocalId(QString{});
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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("CoreTesterFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalId(notebook.localId());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    resource.setUpdateSequenceNum(1);
    resource.setNoteGuid(note.guid());

    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(QByteArray("Fake resource data body"));
    resource.mutableData()->setSize(resource.data()->body()->size());
    resource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.data()->body(),
            QCryptographicHash::Md5));

    resource.setRecognition(qevercloud::Data{});
    resource.mutableRecognition()->setBody(
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

    resource.mutableRecognition()->setSize(
        resource.recognition()->body()->size());

    resource.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(
            *resource.recognition()->body(),
            QCryptographicHash::Md5));

    resource.setAlternateData(qevercloud::Data{});

    resource.mutableAlternateData()->setBody(
        QByteArray("Fake alternate data body"));

    resource.mutableAlternateData()->setSize(
        resource.recognition()->body()->size());

    resource.mutableAlternateData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.alternateData()->body(),
            QCryptographicHash::Md5));

    resource.setMime(QStringLiteral("text/plain"));
    resource.setWidth(1);
    resource.setHeight(1);

    resource.setAttributes(qevercloud::ResourceAttributes{});
    auto & resourceAttributes = *resource.mutableAttributes();

    resourceAttributes.setSourceURL(QStringLiteral("Fake resource source URL"));
    resourceAttributes.setTimestamp(1);
    resourceAttributes.setLatitude(0.0);
    resourceAttributes.setLongitude(0.0);
    resourceAttributes.setAltitude(0.0);

    resourceAttributes.setCameraMake(
        QStringLiteral("Fake resource camera make"));

    resourceAttributes.setCameraModel(
        QStringLiteral("Fake resource camera model"));

    note.setLocalId(QString{});

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addEnResource(resource, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString resourceGuid = resource.guid().value();

    qevercloud::Resource foundResource;
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
    qevercloud::Resource modifiedResource = resource;

    modifiedResource.setUpdateSequenceNum(
        resource.updateSequenceNum().value() + 1);

    modifiedResource.setData(qevercloud::Data{});

    modifiedResource.mutableData()->setBody(
        *resource.data()->body() + QByteArray("_modified"));

    modifiedResource.mutableData()->setSize(
        modifiedResource.data()->body()->size());

    modifiedResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *modifiedResource.data()->body(),
            QCryptographicHash::Md5));

    modifiedResource.setWidth(resource.width().value() + 1);
    modifiedResource.setHeight(resource.height().value() + 1);

    modifiedResource.setRecognition(qevercloud::Data{});
    modifiedResource.mutableRecognition()->setBody(
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

    modifiedResource.mutableData()->setSize(
        modifiedResource.data()->body()->size());

    modifiedResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *modifiedResource.data()->body(),
            QCryptographicHash::Md5));

    modifiedResource.setAlternateData(qevercloud::Data{});

    modifiedResource.mutableAlternateData()->setBody(
        resource.alternateData()->body().value() + QByteArray("_modified"));

    modifiedResource.mutableAlternateData()->setSize(
        modifiedResource.alternateData()->body()->size());

    modifiedResource.mutableAlternateData()->setBodyHash(
        QCryptographicHash::hash(
            *modifiedResource.alternateData()->body(),
            QCryptographicHash::Md5));

    auto & modifiedResourceAttributes =
        modifiedResource.mutableAttributes().value();

    modifiedResourceAttributes.setSourceURL(
        QStringLiteral("Modified source URL"));

    modifiedResourceAttributes.setTimestamp(
        modifiedResourceAttributes.timestamp().value() + 1);

    modifiedResourceAttributes.setLatitude(2.0);
    modifiedResourceAttributes.setLongitude(2.0);
    modifiedResourceAttributes.setAltitude(2.0);

    modifiedResourceAttributes.setCameraMake(
        QStringLiteral("Modified camera make"));

    modifiedResourceAttributes.setCameraModel(
        QStringLiteral("Modified camera model"));

    modifiedResource.setLocalId(QString{});

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
    foundResource = qevercloud::Resource{};
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

    modifiedResource.mutableData()->setBody(std::nullopt);
    modifiedResource.mutableAlternateData()->setBody(std::nullopt);

    VERIFY2(
        modifiedResource == foundResource,
        "Updated and found in the local storage resources without binary data "
            << "don't match: Resource updated in the local storage: "
            << modifiedResource
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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("CoreTesterFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalId(notebook.localId());

    note.setAttributes(qevercloud::NoteAttributes{});
    auto & noteAttributes = *note.mutableAttributes();
    noteAttributes.setSubjectDate(1);
    noteAttributes.setLatitude(1.0);
    noteAttributes.setLongitude(1.0);
    noteAttributes.setAltitude(1.0);
    noteAttributes.setAuthor(QStringLiteral("author"));
    noteAttributes.setSource(QStringLiteral("source"));
    noteAttributes.setSourceURL(QStringLiteral("source URL"));
    noteAttributes.setSourceApplication(QStringLiteral("source application"));
    noteAttributes.setShareDate(2);

    note.setLimits(qevercloud::NoteLimits{});
    auto & noteLimits = *note.mutableLimits();
    noteLimits.setNoteResourceCountMax(50);
    noteLimits.setUploadLimit(268435456);
    noteLimits.setResourceSizeMax(268435456);
    noteLimits.setNoteSizeMax(268435456);
    noteLimits.setUploaded(100);

    note.setLocalId(QString{});

    qevercloud::SharedNote sharedNote;

    sharedNote.mutableLocalData()[QStringLiteral("noteGuid")] =
        note.guid().value();

    sharedNote.setSharerUserID(1);

    sharedNote.setRecipientIdentity(qevercloud::Identity{});
    sharedNote.mutableRecipientIdentity()->setId(qint64{2});
    sharedNote.mutableRecipientIdentity()->setContact(qevercloud::Contact{});

    sharedNote.mutableRecipientIdentity()->mutableContact()->setName(
        QStringLiteral("Contact"));

    sharedNote.mutableRecipientIdentity()->mutableContact()->setId(
        QStringLiteral("Contact id"));

    sharedNote.mutableRecipientIdentity()->mutableContact()->setType(
        qevercloud::ContactType::EVERNOTE);

    sharedNote.mutableRecipientIdentity()->mutableContact()->setPhotoUrl(
        QStringLiteral("url"));

    sharedNote.mutableRecipientIdentity()->mutableContact()->setPhotoLastUpdated(
        qint64{50});

    sharedNote.mutableRecipientIdentity()->mutableContact()->setMessagingPermit(
        QByteArray{"aaa"});

    sharedNote.mutableRecipientIdentity()->mutableContact()->setMessagingPermitExpires(
        qint64{1});

    sharedNote.mutableRecipientIdentity()->setUserId(3);
    sharedNote.mutableRecipientIdentity()->setDeactivated(false);
    sharedNote.mutableRecipientIdentity()->setSameBusiness(true);
    sharedNote.mutableRecipientIdentity()->setBlocked(true);
    sharedNote.mutableRecipientIdentity()->setUserConnected(true);
    sharedNote.mutableRecipientIdentity()->setEventId(qint64{5});
    sharedNote.setPrivilege(qevercloud::SharedNotePrivilegeLevel::FULL_ACCESS);
    sharedNote.setServiceCreated(6);
    sharedNote.setServiceUpdated(7);
    sharedNote.setServiceAssigned(8);

    note.setSharedNotes(QList<qevercloud::SharedNote>() << sharedNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNum(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    note.setTagGuids(QList<qevercloud::Guid>() << *tag.guid());
    note.setTagLocalIds(QStringList() << tag.localId());

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(note, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    resource.setUpdateSequenceNum(1);
    resource.setNoteGuid(note.guid());
    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(QByteArray("Fake resource data body"));
    resource.mutableData()->setSize(resource.data()->body()->size());
    resource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.data()->body(),
            QCryptographicHash::Md5));

    resource.setMime(QStringLiteral("text/plain"));
    resource.setWidth(1);
    resource.setHeight(1);

    resource.setRecognition(qevercloud::Data{});

    resource.mutableRecognition()->setBody(
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

    resource.mutableRecognition()->setSize(
        resource.recognition()->body()->size());

    resource.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(
            *resource.recognition()->body(),
            QCryptographicHash::Md5));

    resource.setAttributes(qevercloud::ResourceAttributes{});
    auto & resourceAttributes = *resource.mutableAttributes();

    resourceAttributes.setSourceURL(QStringLiteral("Fake resource source URL"));
    resourceAttributes.setTimestamp(1);
    resourceAttributes.setLatitude(0.0);
    resourceAttributes.setLongitude(0.0);
    resourceAttributes.setAltitude(0.0);

    resourceAttributes.setCameraMake(
        QStringLiteral("Fake resource camera make"));

    resourceAttributes.setCameraModel(
        QStringLiteral("Fake resource camera model"));

    note.setResources(QList<qevercloud::Resource>() << resource);

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

    qevercloud::Resource foundResource;
    foundResource.setGuid(initialResourceGuid);

    const LocalStorageManager::GetResourceOptions getResourceOptions(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const QString noteGuid = note.guid().value();

    const LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    qevercloud::Note foundNote;
    foundNote.setGuid(noteGuid);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id
     * than the original note which doesn't have one. So use this workaround
     * to ensure the comparison is good for everything without local id
     */
    if (note.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
    }

    VERIFY2(
        note == foundNote,
        "Added and found notes in the local storage don't match"
            << ": Note added to the local storage: " << note
            << "\nNote found in the local storage: " << foundNote);

    // Check Update + Find
    qevercloud::Note modifiedNote = note;
    modifiedNote.setUpdateSequenceNum(note.updateSequenceNum().value() + 1);
    modifiedNote.setTitle(note.title().value() + QStringLiteral("_modified"));
    modifiedNote.setCreated(note.created().value() + 1);
    modifiedNote.setUpdated(note.updated().value() + 1);
    modifiedNote.setLocallyFavorited(true);

    auto & modifiedNoteAttributes = *modifiedNote.mutableAttributes();

    modifiedNoteAttributes.setSubjectDate(2);
    modifiedNoteAttributes.setLatitude(2.0);
    modifiedNoteAttributes.setLongitude(2.0);
    modifiedNoteAttributes.setAltitude(2.0);
    modifiedNoteAttributes.setAuthor(QStringLiteral("modified author"));
    modifiedNoteAttributes.setSource(QStringLiteral("modified source"));
    modifiedNoteAttributes.setSourceURL(QStringLiteral("modified source URL"));

    modifiedNoteAttributes.setSourceApplication(
        QStringLiteral("modified source application"));

    modifiedNoteAttributes.setShareDate(2);

    qevercloud::Tag newTag;
    newTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000050"));
    newTag.setUpdateSequenceNum(1);
    newTag.setName(QStringLiteral("Fake new tag name"));

    QVERIFY2(
        localStorageManager.addTag(newTag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    addNoteTagLocalId(newTag.localId(), modifiedNote);
    addNoteTagGuid(newTag.guid().value(), modifiedNote);

    qevercloud::Resource newResource;
    newResource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000051"));
    newResource.setUpdateSequenceNum(2);
    newResource.setNoteGuid(note.guid());
    newResource.setData(qevercloud::Data{});

    newResource.mutableData()->setBody(
        QByteArray("Fake new resource data body"));

    newResource.mutableData()->setSize(newResource.data()->body()->size());

    newResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *newResource.data()->body(),
            QCryptographicHash::Md5));

    newResource.setMime(QStringLiteral("text/plain"));
    newResource.setWidth(2);
    newResource.setHeight(2);

    newResource.setRecognition(qevercloud::Data{});
    newResource.mutableRecognition()->setBody(
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

    newResource.mutableRecognition()->setSize(
        newResource.recognition()->body()->size());

    newResource.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(
            *newResource.data()->body(),
            QCryptographicHash::Md5));

    newResource.setAttributes(qevercloud::ResourceAttributes{});
    auto & newResourceAttributes = *newResource.mutableAttributes();

    newResourceAttributes.setSourceURL(
        QStringLiteral("Fake resource source URL"));

    newResourceAttributes.setTimestamp(1);
    newResourceAttributes.setLatitude(0.0);
    newResourceAttributes.setLongitude(0.0);
    newResourceAttributes.setAltitude(0.0);

    newResourceAttributes.setCameraMake(
        QStringLiteral("Fake resource camera make"));

    newResourceAttributes.setCameraModel(
        QStringLiteral("Fake resource camera model"));

    newResourceAttributes.setApplicationData(qevercloud::LazyMap{});

    newResourceAttributes.mutableApplicationData()->setKeysOnly(
        QSet<QString>{});

    auto & keysOnly =
        *newResourceAttributes.mutableApplicationData()->mutableKeysOnly();

    keysOnly.reserve(1);
    keysOnly.insert(QStringLiteral("key 1"));

    newResourceAttributes.mutableApplicationData()->setFullMap(
        QMap<QString, QString>{});

    auto & fullMap =
        *newResourceAttributes.mutableApplicationData()->mutableFullMap();

    fullMap[QStringLiteral("key 1 map")] = QStringLiteral("value 1");

    modifiedNote.mutableResources().value().push_back(newResource);

    modifiedNote.setLocalId(QString{});
    modifiedNote.setNotebookLocalId(notebook.localId());

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundResource = qevercloud::Resource();
    foundResource.setGuid(newResource.guid());

    QVERIFY2(
        localStorageManager.findEnResource(
            foundResource, getResourceOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundResource.setNoteLocalId(QString{});

    VERIFY2(
        foundResource == newResource,
        "Something is wrong with the new resource which should have been added "
            << "to the local storage along with the note update: it is not "
            << "equal to the original resource: original resource: "
            << newResource << "\nfound resource: " << foundResource);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local id
     */
    if (modifiedNote.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
    }

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match"
            << ": Note updated in the local storage: " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Check that tags are not touched if update tags flag is not set on attempt
    // to update note
    QStringList tagLocalIdsBeforeUpdate = noteTagLocalIds(modifiedNote);

    QStringList tagGuidsBeforeUpdate =
        modifiedNote.tagGuids().value_or(QStringList());

    removeNoteTagLocalId(newTag.localId(), modifiedNote);
    removeNoteTagGuid(*newTag.guid(), modifiedNote);

    // Modify something about the note to make the test a little more
    // interesting
    modifiedNote.setTitle(
        modifiedNote.title().value() + QStringLiteral("_modified_again"));

    modifiedNote.setLocallyFavorited(false);
    modifiedNote.setUpdated(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = qevercloud::Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local id
     */
    if (modifiedNote.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
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
    modifiedNote.setTagLocalIds(tagLocalIdsBeforeUpdate);

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
    auto previousModifiedNoteResources =
        modifiedNote.resources().value_or(QList<qevercloud::Resource>());

    removeNoteResource(newResource.localId(), modifiedNote);

    // Modify something about the note to make the test a little more
    // interesting
    modifiedNote.setTitle(
        modifiedNote.title().value() + QStringLiteral("_modified_once_again"));

    modifiedNote.setLocallyFavorited(true);
    modifiedNote.setUpdated(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = qevercloud::Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local id
     */
    if (modifiedNote.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
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
    removeNoteResource(newResource.localId(), modifiedNote);

    modifiedNote.setUpdated(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = qevercloud::Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local id
     */
    if (modifiedNote.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
    }

    /**
     * Found note should not be equal to the modified note because their
     * resources should be different; after restoring the previous resources
     * list to the modified note the two notes should become equal
     */
    VERIFY2(
        modifiedNote != foundNote,
        "Detected unexpectedly equal notes: locally modified notes which had "
            << "its resources list modified but not updated in the local "
            << "storage and the note found in the local storage: Note updated "
            << "in the local storage (with resource removed): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    modifiedNote.setResources(previousModifiedNoteResources);

    VERIFY2(
        modifiedNote == foundNote,
        "Updated and found in the local storage notes don't match: Note "
            << "updated in the local storage (without resource metadata after "
            << "which resources were manually restored): " << modifiedNote
            << "\nNote found in the local storage: " << foundNote);

    // Check that resource binary data is not touched unless update resource
    // binary data flag is set on attempt to update note
    newResource.setData(qevercloud::Data{});

    newResource.mutableData()->setBody(
        QByteArray("Fake modified new resource data body"));

    newResource.mutableData()->setSize(
        newResource.data()->body()->size());

    putNoteResource(newResource, modifiedNote);

    modifiedNote.setUpdated(QDateTime::currentMSecsSinceEpoch());

    updateNoteOptions = LocalStorageManager::UpdateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags |
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);

    QVERIFY2(
        localStorageManager.updateNote(
            modifiedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNote = qevercloud::Note();
    foundNote.setGuid(modifiedNote.guid());

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    /**
     * NOTE: foundNote was searched by guid and might have another local id is
     * the original note doesn't have one. So use this workaround to ensure
     * the comparison is good for everything without local id
     */
    if (modifiedNote.localId().isEmpty()) {
        foundNote.setLocalId(QString{});
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
    qevercloud::Note newNote;
    newNote.setNotebookGuid(notebook.guid());
    newNote.setTitle(QStringLiteral("New note"));

    addNoteTagGuid(tag.guid().value(), newNote);
    addNoteTagLocalId(tag.localId(), newNote);

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
    QHash<QString, int> noteCountsPerTagLocalId;

    QVERIFY2(
        localStorageManager.noteCountsPerAllTags(
            noteCountsPerTagLocalId, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        noteCountsPerTagLocalId.size() == 2,
        qPrintable(QString::fromUtf8("Unexpected amount of tag local ids "
                                     "within the hash of note counts by tag "
                                     "local id: expected 2, got %1")
                       .arg(noteCountsPerTagLocalId.size())));

    auto firstTagNoteCountIt = noteCountsPerTagLocalId.find(tag.localId());

    QVERIFY2(
        firstTagNoteCountIt != noteCountsPerTagLocalId.end(),
        qPrintable(QStringLiteral(
            "Can't find the note count for first tag's local id")));

    QVERIFY2(
        firstTagNoteCountIt.value() == 2,
        qPrintable(QString::fromUtf8("Unexpected note count for the first "
                                     "tag: expected 2, got %1")
                       .arg(firstTagNoteCountIt.value())));

    auto secondTagNoteCountIt =
        noteCountsPerTagLocalId.find(newTag.localId());

    QVERIFY2(
        secondTagNoteCountIt != noteCountsPerTagLocalId.end(),
        qPrintable(QStringLiteral(
            "Can't find the note count for second tag's local id")));

    QVERIFY2(
        secondTagNoteCountIt.value() == 1,
        qPrintable(
            QString::fromUtf8(
                "Unexpected note count for the second tag: expected 1, got %1")
                .arg(secondTagNoteCountIt.value())));

    // noteCountPerNotebooksAndTags to return 1 for new tag
    QStringList notebookLocalIds;
    notebookLocalIds << notebook.localId();
    QStringList tagLocalIds;
    tagLocalIds << newTag.localId();

    count = localStorageManager.noteCountPerNotebooksAndTags(
        notebookLocalIds, tagLocalIds, errorMessage);

    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 1,
        qPrintable(QString::fromUtf8("noteCountPerNotebooksAndTags returned "
                                     "result %1 different from the expected "
                                     "one (1)")
                       .arg(count)));

    // noteCountPerNotebooksAndTags to return 2 for old tag
    tagLocalIds << tag.localId();

    count = localStorageManager.noteCountPerNotebooksAndTags(
        notebookLocalIds, tagLocalIds, errorMessage);

    QVERIFY2(count >= 0, qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        count == 2,
        qPrintable(QString::fromUtf8("noteCountPerNotebooksAndTags returned "
                                     "result %1 different from the expected "
                                     "one (2)")
                       .arg(count)));

    // Check Delete + Find and check deleted flag
    modifiedNote.setActive(false);
    modifiedNote.setDeleted(1);
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
        foundNote.active() && !*foundNote.active(),
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
    foundResource = qevercloud::Resource();
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

    qevercloud::LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNum(1);
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

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setLinkedNotebookGuid(*linkedNotebook.guid());
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);
    notebook.setDefaultNotebook(true);
    notebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;
    notebook.setPublishing(qevercloud::Publishing{});
    notebook.mutablePublishing()->setUri(QStringLiteral("Fake publishing uri"));
    notebook.mutablePublishing()->setOrder(qevercloud::NoteSortOrder::CREATED);
    notebook.mutablePublishing()->setAscending(true);

    notebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    notebook.setBusinessNotebook(qevercloud::BusinessNotebook{});

    notebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    notebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    notebook.mutableBusinessNotebook()->setRecommended(true);

    // NotebookRestrictions
    notebook.setRestrictions(qevercloud::NotebookRestrictions{});
    auto & notebookRestrictions = *notebook.mutableRestrictions();
    notebookRestrictions.setNoReadNotes(false);
    notebookRestrictions.setNoCreateNotes(false);
    notebookRestrictions.setNoUpdateNotes(false);
    notebookRestrictions.setNoExpungeNotes(true);
    notebookRestrictions.setNoShareNotes(false);
    notebookRestrictions.setNoEmailNotes(false);
    notebookRestrictions.setNoSendMessageToRecipients(false);
    notebookRestrictions.setNoUpdateNotebook(false);
    notebookRestrictions.setNoExpungeNotebook(true);
    notebookRestrictions.setNoSetDefaultNotebook(false);
    notebookRestrictions.setNoSetNotebookStack(false);
    notebookRestrictions.setNoPublishToPublic(false);
    notebookRestrictions.setNoPublishToBusinessLibrary(true);
    notebookRestrictions.setNoCreateTags(false);
    notebookRestrictions.setNoUpdateTags(false);
    notebookRestrictions.setNoExpungeTags(true);
    notebookRestrictions.setNoSetParentTag(false);
    notebookRestrictions.setNoCreateSharedNotebooks(false);
    notebookRestrictions.setNoUpdateNotebook(false);

    notebookRestrictions.setUpdateWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED);

    notebookRestrictions.setExpungeWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS);

    qevercloud::SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(1);
    sharedNotebook.setNotebookGuid(notebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setServiceCreated(1);
    sharedNotebook.setServiceUpdated(1);

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));

    sharedNotebook.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    sharedNotebook.setRecipientSettings(
        qevercloud::SharedNotebookRecipientSettings{});

    sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(false);

    notebook.setSharedNotebooks(
        QList<qevercloud::SharedNotebook>() << sharedNotebook);

    ErrorString errorMessage;
    QVERIFY2(
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalId(notebook.localId());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
    tag.setUpdateSequenceNum(1);
    tag.setName(QStringLiteral("Fake tag name"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    addNoteTagGuid(*tag.guid(), note);
    addNoteTagLocalId(tag.localId(), note);

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(note, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check Find
    const QString initialNoteGuid =
        QStringLiteral("00000000-0000-0000-c000-000000000049");

    qevercloud::Note foundNote;
    foundNote.setGuid(initialNoteGuid);

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());

    const auto notebookLinkedNotebookGuid = notebook.linkedNotebookGuid();
    if (notebookLinkedNotebookGuid && !notebookLinkedNotebookGuid->isEmpty()) {
        foundNotebook.setLinkedNotebookGuid(notebookLinkedNotebookGuid);
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
    qevercloud::Notebook foundByNameNotebook;
    foundByNameNotebook.setLocalId(QString{});
    foundByNameNotebook.setName(notebook.name());

    if (notebookLinkedNotebookGuid && !notebookLinkedNotebookGuid->isEmpty()) {
        foundByNameNotebook.setLinkedNotebookGuid(notebookLinkedNotebookGuid);
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

    if (notebookLinkedNotebookGuid && !notebookLinkedNotebookGuid->isEmpty()) {
        // Check Find by linked notebook guid
        qevercloud::Notebook foundByLinkedNotebookGuidNotebook;
        foundByLinkedNotebookGuidNotebook.setLocalId(QString{});

        foundByLinkedNotebookGuidNotebook.setLinkedNotebookGuid(
            notebookLinkedNotebookGuid);

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
    qevercloud::Notebook defaultNotebook;

    QVERIFY2(
        localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check FindLastUsedNotebook (failure expected)
    qevercloud::Notebook lastUsedNotebook;

    VERIFY2(
        !localStorageManager.findLastUsedNotebook(
            lastUsedNotebook, errorMessage),
        "Found some last used notebook which shouldn't have been found: "
            << lastUsedNotebook);

    // Check FindDefaultOrLastUsedNotebook
    qevercloud::Notebook defaultOrLastUsedNotebook;

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
    qevercloud::Notebook modifiedNotebook(notebook);

    modifiedNotebook.setUpdateSequenceNum(
        notebook.updateSequenceNum().value() + 1);

    modifiedNotebook.setLinkedNotebookGuid(QString{});

    modifiedNotebook.setName(
        notebook.name().value() + QStringLiteral("_modified"));

    modifiedNotebook.setDefaultNotebook(false);
    modifiedNotebook.mutableLocalData()[QStringLiteral("isLastUsed")] = true;

    modifiedNotebook.setServiceUpdated(
        notebook.serviceUpdated().value() + 1);

    modifiedNotebook.setPublishing(qevercloud::Publishing{});

    modifiedNotebook.mutablePublishing()->setUri(
        notebook.publishing()->uri().value() +
        QStringLiteral("_modified"));

    modifiedNotebook.mutablePublishing()->setAscending(
        !notebook.publishing()->ascending().value());

    modifiedNotebook.mutablePublishing()->setPublicDescription(
        notebook.publishing()->publicDescription().value() +
        QStringLiteral("_modified"));

    modifiedNotebook.setStack(
        notebook.stack().value() + QStringLiteral("_modified"));

    modifiedNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});

    modifiedNotebook.mutableBusinessNotebook()->setNotebookDescription(
        notebook.businessNotebook()->notebookDescription().value() +
        QStringLiteral("_modified"));

    modifiedNotebook.mutableBusinessNotebook()->setRecommended(
        !notebook.businessNotebook()->recommended().value());

    modifiedNotebook.setRestrictions(qevercloud::NotebookRestrictions{});

    auto & modifiedNotebookRestrictions =
        *modifiedNotebook.mutableRestrictions();

    modifiedNotebookRestrictions.setNoExpungeNotes(true);
    modifiedNotebookRestrictions.setNoEmailNotes(true);
    modifiedNotebookRestrictions.setNoPublishToPublic(true);

    modifiedNotebook.setLocallyFavorited(true);

    QVERIFY2(
        localStorageManager.updateNotebook(modifiedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundNotebook = qevercloud::Notebook();
    foundNotebook.setGuid(modifiedNotebook.guid());

    const auto modifiedNotebookLinkedNotebookGuid =
        modifiedNotebook.linkedNotebookGuid();

    if (modifiedNotebookLinkedNotebookGuid &&
        !modifiedNotebookLinkedNotebookGuid->isEmpty()) {
        foundNotebook.setLinkedNotebookGuid(
            modifiedNotebookLinkedNotebookGuid);
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
    defaultNotebook = qevercloud::Notebook();

    VERIFY2(
        !localStorageManager.findDefaultNotebook(defaultNotebook, errorMessage),
        "Found some default notebook which shouldn't have been found: "
            << defaultNotebook);

    // Check FindLastUsedNotebook
    lastUsedNotebook = qevercloud::Notebook();

    QVERIFY2(
        localStorageManager.findLastUsedNotebook(
            lastUsedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // Check FindDefaultOrLastUsedNotebook
    defaultOrLastUsedNotebook = qevercloud::Notebook();

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
    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("TestFindNotebookByNameWithDiacriticsFakeUser"),
        Account::Type::Local};

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook1;
    notebook1.setGuid(UidGenerator::Generate());
    notebook1.setUpdateSequenceNum(1);
    notebook1.setName(QStringLiteral("notebook"));
    notebook1.setDefaultNotebook(false);
    notebook1.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    qevercloud::Notebook notebook2;
    notebook2.setGuid(UidGenerator::Generate());
    notebook2.setUpdateSequenceNum(2);
    notebook2.setName(QStringLiteral("notébook"));
    notebook2.setDefaultNotebook(false);
    notebook2.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    ErrorString errorMessage;

    QVERIFY2(
        localStorageManager.addNotebook(notebook1, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        localStorageManager.addNotebook(notebook2, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Notebook notebookToFind;
    notebookToFind.setLocalId(QString{});
    notebookToFind.setName(notebook1.name());

    QVERIFY2(
        localStorageManager.findNotebook(notebookToFind, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        notebookToFind == notebook1,
        "Found wrong notebook by name: expected notebook: "
            << notebook1 << "\nActually found notebook: " << notebookToFind);

    notebookToFind = qevercloud::Notebook();
    notebookToFind.setLocalId(QString{});
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

    qevercloud::User user;
    user.setId(1);
    user.setUsername(QStringLiteral("fake_user_username"));
    user.setEmail(QStringLiteral("fake_user _mail"));
    user.setName(QStringLiteral("fake_user_name"));
    user.setTimezone(QStringLiteral("fake_user_timezone"));
    user.setPrivilege(qevercloud::PrivilegeLevel::NORMAL);
    user.setCreated(2);
    user.setUpdated(3);
    user.setActive(true);

    qevercloud::UserAttributes userAttributes;

    userAttributes.setDefaultLocationName(
        QStringLiteral("fake_default_location_name"));

    userAttributes.setDefaultLatitude(1.0);
    userAttributes.setDefaultLongitude(2.0);
    userAttributes.setPreactivation(false);
    QList<QString> viewedPromotions;
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 1"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 2"));
    viewedPromotions.push_back(QStringLiteral("Viewed promotion 3"));
    userAttributes.setViewedPromotions(viewedPromotions);

    userAttributes.setIncomingEmailAddress(
        QStringLiteral("fake_incoming_email_address"));

    QList<QString> recentEmailAddresses;
    recentEmailAddresses.push_back(QStringLiteral("recent_email_address_1"));
    recentEmailAddresses.push_back(QStringLiteral("recent_email_address_2"));
    userAttributes.setRecentMailedAddresses(recentEmailAddresses);
    userAttributes.setComments(QStringLiteral("Fake comments"));
    userAttributes.setDateAgreedToTermsOfService(1);
    userAttributes.setMaxReferrals(3);
    userAttributes.setRefererCode(QStringLiteral("fake_referer_code"));
    userAttributes.setSentEmailDate(5);
    userAttributes.setSentEmailCount(4);
    userAttributes.setDailyEmailLimit(2);
    userAttributes.setEmailOptOutDate(6);
    userAttributes.setPartnerEmailOptInDate(7);
    userAttributes.setPreferredLanguage(QStringLiteral("ru"));
    userAttributes.setPreferredCountry(QStringLiteral("Russia"));
    userAttributes.setClipFullPage(true);
    userAttributes.setTwitterUserName(QStringLiteral("fake_twitter_username"));
    userAttributes.setTwitterId(QStringLiteral("fake_twitter_id"));
    userAttributes.setGroupName(QStringLiteral("fake_group_name"));
    userAttributes.setRecognitionLanguage(QStringLiteral("ru"));

    userAttributes.setReferralProof(
        QStringLiteral("I_have_no_idea_what_this_means"));

    userAttributes.setEducationalDiscount(false);
    userAttributes.setBusinessAddress(QStringLiteral("fake_business_address"));
    userAttributes.setHideSponsorBilling(true);
    userAttributes.setUseEmailAutoFiling(true);

    userAttributes.setReminderEmailConfig(
        qevercloud::ReminderEmailConfig::DO_NOT_SEND);

    user.setAttributes(std::move(userAttributes));

    qevercloud::BusinessUserInfo businessUserInfo;
    businessUserInfo.setBusinessId(1);
    businessUserInfo.setBusinessName(QStringLiteral("Fake business name"));
    businessUserInfo.setRole(qevercloud::BusinessUserRole::NORMAL);
    businessUserInfo.setEmail(QStringLiteral("Fake business email"));

    user.setBusinessUserInfo(std::move(businessUserInfo));

    qevercloud::Accounting accounting;
    accounting.setUploadLimitEnd(9);
    accounting.setUploadLimitNextMonth(1200);
    accounting.setPremiumServiceStatus(qevercloud::PremiumOrderStatus::PENDING);

    accounting.setPremiumOrderNumber(
        QStringLiteral("Fake premium order number"));

    accounting.setPremiumCommerceService(
        QStringLiteral("Fake premium commerce service"));

    accounting.setPremiumServiceStart(8);

    accounting.setPremiumServiceSKU(
        QStringLiteral("Fake code associated with the purchase"));

    accounting.setLastSuccessfulCharge(7);
    accounting.setLastFailedCharge(5);
    accounting.setLastFailedChargeReason(QStringLiteral("No money, no honey"));
    accounting.setNextPaymentDue(12);
    accounting.setPremiumLockUntil(11);
    accounting.setUpdated(10);

    accounting.setPremiumSubscriptionNumber(
        QStringLiteral("Fake premium subscription number"));

    accounting.setLastRequestedCharge(9);
    accounting.setCurrency(QStringLiteral("USD"));
    accounting.setUnitPrice(100);
    accounting.setUnitDiscount(2);
    accounting.setNextChargeDate(12);

    user.setAccounting(std::move(accounting));

    qevercloud::AccountLimits accountLimits;
    accountLimits.setUserNotebookCountMax(10);
    accountLimits.setUploadLimit(2048);
    accountLimits.setNoteResourceCountMax(10);
    accountLimits.setUserSavedSearchesMax(100);
    accountLimits.setNoteSizeMax(4096);
    accountLimits.setUserMailLimitDaily(20);
    accountLimits.setNoteTagCountMax(20);
    accountLimits.setResourceSizeMax(4096);
    accountLimits.setUserTagCountMax(200);

    user.setAccountLimits(std::move(accountLimits));

    ErrorString errorMessage;

    // Check Add + Find
    QVERIFY2(
        localStorageManager.addUser(user, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const qint32 initialUserId = user.id().value();

    qevercloud::User foundUser;
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
    qevercloud::User modifiedUser;
    modifiedUser.setId(user.id());

    modifiedUser.setUsername(
        user.username().value() + QStringLiteral("_modified"));

    modifiedUser.setEmail(user.email().value() + QStringLiteral("_modified"));
    modifiedUser.setName(user.name().value() + QStringLiteral("_modified"));

    modifiedUser.setTimezone(
        user.timezone().value() + QStringLiteral("_modified"));

    modifiedUser.setPrivilege(user.privilege());
    modifiedUser.setCreated(user.created());
    modifiedUser.setUpdated(user.updated().value() + 1);
    modifiedUser.setActive(true);

    qevercloud::UserAttributes modifiedUserAttributes;
    modifiedUserAttributes = *user.attributes();

    modifiedUserAttributes.setDefaultLocationName(
        user.attributes()->defaultLocationName().value() +
        QStringLiteral("_modified"));

    modifiedUserAttributes.setComments(
        user.attributes()->comments().value() +
        QStringLiteral("_modified"));

    modifiedUserAttributes.setPreferredCountry(
        user.attributes()->preferredCountry().value() +
        QStringLiteral("_modified"));

    modifiedUserAttributes.setBusinessAddress(
        user.attributes()->businessAddress().value() +
        QStringLiteral("_modified"));

    modifiedUser.setAttributes(std::move(modifiedUserAttributes));

    qevercloud::BusinessUserInfo modifiedBusinessUserInfo;
    modifiedBusinessUserInfo = user.businessUserInfo().value();

    modifiedBusinessUserInfo.setBusinessName(
        user.businessUserInfo()->businessName().value() +
        QStringLiteral("_modified"));

    modifiedBusinessUserInfo.setEmail(
        user.businessUserInfo()->email().value() +
        QStringLiteral("_modified"));

    modifiedUser.setBusinessUserInfo(std::move(modifiedBusinessUserInfo));

    qevercloud::Accounting modifiedAccounting;
    modifiedAccounting = user.accounting().value();

    modifiedAccounting.setPremiumOrderNumber(
        user.accounting()->premiumOrderNumber().value() +
        QStringLiteral("_modified"));

    modifiedAccounting.setPremiumSubscriptionNumber(
        user.accounting()->premiumSubscriptionNumber().value() +
        QStringLiteral("_modified"));

    modifiedAccounting.setUpdated(user.accounting()->updated().value() + 1);

    modifiedUser.setAccounting(std::move(modifiedAccounting));

    qevercloud::AccountLimits modifiedAccountLimits;
    modifiedAccountLimits = user.accountLimits().value();
    modifiedAccountLimits.setNoteTagCountMax(2);
    modifiedAccountLimits.setUserLinkedNotebookMax(2);
    modifiedAccountLimits.setUserNotebookCountMax(2);

    modifiedUser.setAccountLimits(std::move(modifiedAccountLimits));

    QVERIFY2(
        localStorageManager.updateUser(modifiedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundUser = qevercloud::User{};
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
    modifiedUser.setDeleted(5);

    QVERIFY2(
        localStorageManager.deleteUser(modifiedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    foundUser = qevercloud::User{};
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

    foundUser = qevercloud::User{};
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

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("LocalStorageManagerSequentialUpdatesTestFakeUser"),
        Account::Type::Evernote,
        0};

    LocalStorageManager localStorageManager(account, startupOptions);

    // 2) Create User
    qevercloud::User user;
    user.setId(1);
    user.setUsername(QStringLiteral("checker"));
    user.setEmail(QStringLiteral("mail@checker.com"));
    user.setTimezone(QStringLiteral("Europe/Moscow"));
    user.setCreated(QDateTime::currentMSecsSinceEpoch());
    user.setUpdated(QDateTime::currentMSecsSinceEpoch());
    user.setActive(true);

    qevercloud::UserAttributes userAttributes;
    userAttributes.setDefaultLocationName(QStringLiteral("Default location"));
    userAttributes.setComments(QStringLiteral("My comment"));
    userAttributes.setPreferredLanguage(QStringLiteral("English"));
    userAttributes.setViewedPromotions(
        QStringList() << QStringLiteral("Promotion #1")
        << QStringLiteral("Promotion #2") << QStringLiteral("Promotion #3"));

    userAttributes.setRecentMailedAddresses(
        QStringList() << QStringLiteral("Recent mailed address #1")
        << QStringLiteral("Recent mailed address #2")
        << QStringLiteral("Recent mailed address #3"));

    user.setAttributes(std::move(userAttributes));

    qevercloud::Accounting accounting;
    accounting.setPremiumOrderNumber(QStringLiteral("Premium order number"));

    accounting.setPremiumSubscriptionNumber(
        QStringLiteral("Premium subscription number"));

    accounting.setUpdated(QDateTime::currentMSecsSinceEpoch());

    user.setAccounting(std::move(accounting));

    qevercloud::BusinessUserInfo businessUserInfo;
    businessUserInfo.setBusinessName(QStringLiteral("Business name"));
    businessUserInfo.setEmail(QStringLiteral("Business email"));

    user.setBusinessUserInfo(std::move(businessUserInfo));

    qevercloud::AccountLimits accountLimits;
    accountLimits.setNoteResourceCountMax(20);
    accountLimits.setUserNoteCountMax(200);
    accountLimits.setUserSavedSearchesMax(100);

    user.setAccountLimits(std::move(accountLimits));

    ErrorString errorMessage;

    // 3) Add user to local storage
    QVERIFY2(
        localStorageManager.addUser(user, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 4) Create new user without all the supplementary data but with the same
    // id and update it in local storage
    qevercloud::User updatedUser;
    updatedUser.setId(1);
    updatedUser.setUsername(QStringLiteral("checker"));
    updatedUser.setEmail(QStringLiteral("mail@checker.com"));
    updatedUser.setPrivilege(qevercloud::PrivilegeLevel::NORMAL);
    updatedUser.setCreated(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setUpdated(QDateTime::currentMSecsSinceEpoch());
    updatedUser.setActive(true);

    QVERIFY2(
        localStorageManager.updateUser(updatedUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 5) Find this user in local storage, check whether it has user attributes,
    // accounting, business user info and premium info (it shouldn't)
    qevercloud::User foundUser;
    foundUser.setId(1);

    QVERIFY2(
        localStorageManager.findUser(foundUser, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundUser.attributes(),
        "Updated user found the in the local storage "
            << "still has user attributes while it "
            << "shouldn't have them after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.accounting(),
        "Updated user found in the local storage "
            << "still has accounting while it shouldn't "
            << "have it after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.businessUserInfo(),
        "Updated user found in the local storage "
            << "still has business user info "
            << "while it shouldn't have it after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    VERIFY2(
        !foundUser.accountLimits(),
        "Updated user found in the local storage "
            << "still has account limits while it "
            << "shouldn't have them after the update"
            << ": initial user: " << user << "\nUpdated user: " << updatedUser
            << "\nFound user: " << foundUser);

    // 6) Create Notebook with restrictions and shared notebooks
    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);
    notebook.setDefaultNotebook(true);
    notebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    notebook.setPublishing(qevercloud::Publishing{});
    notebook.mutablePublishing()->setUri(QStringLiteral("Fake publishing uri"));
    notebook.mutablePublishing()->setOrder(qevercloud::NoteSortOrder::CREATED);
    notebook.mutablePublishing()->setAscending(true);
    notebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    notebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
    notebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));
    notebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);
    notebook.mutableBusinessNotebook()->setRecommended(true);

    // NotebookRestrictions
    notebook.setRestrictions(qevercloud::NotebookRestrictions{});
    auto & notebookRestrictions = *notebook.mutableRestrictions();
    notebookRestrictions.setNoReadNotes(false);
    notebookRestrictions.setNoCreateNotes(false);
    notebookRestrictions.setNoUpdateNotes(false);
    notebookRestrictions.setNoExpungeNotes(true);
    notebookRestrictions.setNoShareNotes(false);
    notebookRestrictions.setNoEmailNotes(true);
    notebookRestrictions.setNoSendMessageToRecipients(false);
    notebookRestrictions.setNoUpdateNotebook(false);
    notebookRestrictions.setNoExpungeNotebook(true);
    notebookRestrictions.setNoSetDefaultNotebook(false);
    notebookRestrictions.setNoSetNotebookStack(true);
    notebookRestrictions.setNoPublishToPublic(false);
    notebookRestrictions.setNoPublishToBusinessLibrary(true);
    notebookRestrictions.setNoCreateTags(false);
    notebookRestrictions.setNoUpdateTags(false);
    notebookRestrictions.setNoExpungeTags(true);
    notebookRestrictions.setNoSetParentTag(false);
    notebookRestrictions.setNoCreateSharedNotebooks(false);
    notebookRestrictions.setNoUpdateNotebook(false);
    notebookRestrictions.setUpdateWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED);
    notebookRestrictions.setExpungeWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS);

    qevercloud::SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(1);
    sharedNotebook.setNotebookGuid(notebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setServiceCreated(1);
    sharedNotebook.setServiceUpdated(1);

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    sharedNotebook.setRecipientSettings(
        qevercloud::SharedNotebookRecipientSettings{});
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(false);

    notebook.setSharedNotebooks(
        QList<qevercloud::SharedNotebook>() << sharedNotebook);

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 7) Update notebook: remove restrictions and shared notebooks
    qevercloud::Notebook updatedNotebook;
    updatedNotebook.setLocalId(notebook.localId());
    updatedNotebook.setGuid(notebook.guid());
    updatedNotebook.setUpdateSequenceNum(1);
    updatedNotebook.setName(QStringLiteral("Fake notebook name"));
    updatedNotebook.setServiceCreated(1);
    updatedNotebook.setServiceUpdated(1);
    updatedNotebook.setDefaultNotebook(true);
    updatedNotebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    updatedNotebook.setPublishing(qevercloud::Publishing{});
    updatedNotebook.mutablePublishing()->setUri(
        QStringLiteral("Fake publishing uri"));
    updatedNotebook.mutablePublishing()->setOrder(
        qevercloud::NoteSortOrder::CREATED);
    updatedNotebook.mutablePublishing()->setAscending(true);

    updatedNotebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    updatedNotebook.setPublished(true);
    updatedNotebook.setStack(QStringLiteral("Fake notebook stack"));

    updatedNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
    updatedNotebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    updatedNotebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    updatedNotebook.mutableBusinessNotebook()->setRecommended(true);

    QVERIFY2(
        localStorageManager.updateNotebook(updatedNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 8) Find notebook, ensure it doesn't have neither restrictions
    // nor shared notebooks

    qevercloud::Notebook foundNotebook;
    foundNotebook.setGuid(notebook.guid());

    QVERIFY2(
        localStorageManager.findNotebook(foundNotebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundNotebook.sharedNotebooks() ||
        foundNotebook.sharedNotebooks()->isEmpty(),
        "Updated notebook found in the local "
            << "storage has shared notebooks "
            << "while it shouldn't have them"
            << ", original notebook: " << notebook << "\nUpdated notebook: "
            << updatedNotebook << "\nFound notebook: " << foundNotebook);

    VERIFY2(
        !foundNotebook.restrictions(),
        "Updated notebook found in the local "
            << "storage has restrictions "
            << "while it shouldn't have them"
            << ", original notebook: " << notebook << "\nUpdated notebook: "
            << updatedNotebook << "\nFound notebook: " << foundNotebook);

    // 9) Create tag
    qevercloud::Tag tag;
    tag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    tag.setUpdateSequenceNum(1);
    tag.setName(QStringLiteral("Fake tag name"));

    QVERIFY2(
        localStorageManager.addTag(tag, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 10) Create note, add this tag to it along with some resource
    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());

    qevercloud::Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000044"));
    resource.setUpdateSequenceNum(1);
    resource.setNoteGuid(note.guid());

    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(QByteArray("Fake resource data body"));
    resource.mutableData()->setSize(resource.data()->body()->size());
    resource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.data()->body(),
            QCryptographicHash::Md5));

    addNoteResource(resource, note);
    addNoteTagGuid(*tag.guid(), note);
    note.setNotebookLocalId(updatedNotebook.localId());

    QVERIFY2(
        localStorageManager.addNote(note, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 11) Update note, remove tag guid and resource
    qevercloud::Note updatedNote;
    updatedNote.setLocalId(note.localId());
    updatedNote.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
    updatedNote.setUpdateSequenceNum(1);
    updatedNote.setTitle(QStringLiteral("Fake note title"));

    updatedNote.setContent(
        QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    updatedNote.setCreated(1);
    updatedNote.setUpdated(1);
    updatedNote.setActive(true);
    updatedNote.setNotebookGuid(notebook.guid());
    updatedNote.setNotebookLocalId(notebook.localId());

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
    qevercloud::Note foundNote;
    foundNote.setLocalId(updatedNote.localId());
    foundNote.setGuid(updatedNote.guid());

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    QVERIFY2(
        localStorageManager.findNote(foundNote, getNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        !foundNote.tagGuids() || foundNote.tagGuids()->isEmpty(),
        "Updated note found in local storage "
            << "has tag guids while it shouldn't have them"
            << ", original note: " << note << "\nUpdated note: " << updatedNote
            << "\nFound note: " << foundNote);

    VERIFY2(
        !foundNote.resources() || foundNote.resources()->isEmpty(),
        "Updated note found in local storage "
            << "has resources while it shouldn't have them"
            << ", original note: " << note << "\nUpdated note: " << updatedNote
            << "\nFound note: " << foundNote);

    // 13) Add resource attributes to the resource and add resource to note
    resource.setAttributes(qevercloud::ResourceAttributes{});

    auto & resourceAttributes = *resource.mutableAttributes();
    resourceAttributes.setApplicationData(qevercloud::LazyMap{});

    auto & resourceAppData = *resourceAttributes.mutableApplicationData();
    resourceAppData.setKeysOnly(
        QSet<QString>() << QStringLiteral("key_1")
        << QStringLiteral("key_2") << QStringLiteral("key_3"));

    resourceAppData.setFullMap(QMap<QString, QString>{});

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_1")] =
        QStringLiteral("value_1");

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_2")] =
        QStringLiteral("value_2");

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_3")] =
        QStringLiteral("value_3");

    addNoteResource(resource, updatedNote);

    QVERIFY2(
        localStorageManager.updateNote(
            updatedNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 14) Remove resource attributes from note's resource and update it again
    auto resources = *updatedNote.resources();

    VERIFY2(
        !resources.empty(),
        "Note returned empty list of resources "
            << "while it should have contained at least "
            << "one entry, updated note: " << updatedNote);

    auto & updatedResource = resources[0];

    auto & underlyngResourceAttributes =
        updatedResource.mutableAttributes().value();

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

    resources = foundNote.resources().value();

    VERIFY2(
        !resources.empty(),
        "Note returned empty list of resources "
            << "while it should have contained "
            << "at least one entry, found note: " << foundNote);

    auto & foundResource = resources[0];
    auto & foundResourceAttributes = foundResource.attributes().value();

    VERIFY2(
        !foundResourceAttributes.applicationData(),
        "Resource from updated note has application "
            << "data while it shouldn't have it, found resource: "
            << foundResource);
}

void TestAccountHighUsnInLocalStorage()
{
    // 1) Create LocalStorageManager

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("LocalStorageManagerAccountHighUsnTestFakeUser"),
        Account::Type::Evernote,
        0};

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Verify that account high USN is initially zero (since all tables
    // are empty)

    qint32 initialUsn = localStorageManager.accountHighUsn({}, errorMessage);
    QVERIFY2(initialUsn == 0, qPrintable(errorMessage.nonLocalizedString()));
    qint32 currentUsn = initialUsn;

    // 3) Create some user's own notebooks with different USNs

    qevercloud::Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setUpdateSequenceNum(currentUsn++);
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setServiceCreated(QDateTime::currentMSecsSinceEpoch());
    firstNotebook.setServiceUpdated(firstNotebook.serviceCreated());
    firstNotebook.setDefaultNotebook(true);
    firstNotebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    qevercloud::Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setUpdateSequenceNum(currentUsn++);
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setServiceCreated(QDateTime::currentMSecsSinceEpoch());
    secondNotebook.setServiceUpdated(secondNotebook.serviceCreated());
    secondNotebook.setDefaultNotebook(false);
    secondNotebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

    qevercloud::Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setUpdateSequenceNum(currentUsn++);
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setServiceCreated(QDateTime::currentMSecsSinceEpoch());
    thirdNotebook.setServiceUpdated(thirdNotebook.serviceCreated());
    thirdNotebook.setDefaultNotebook(false);
    thirdNotebook.mutableLocalData()[QStringLiteral("isLastUsed")] = true;

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
        accountHighUsn == thirdNotebook.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdNotebook.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 5) Create some user's own tags with different USNs

    qevercloud::Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setUpdateSequenceNum(currentUsn++);

    qevercloud::Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setUpdateSequenceNum(currentUsn++);

    qevercloud::Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setUpdateSequenceNum(currentUsn++);
    thirdTag.setParentGuid(secondTag.guid());
    thirdTag.setParentTagLocalId(secondTag.localId());

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
        accountHighUsn == thirdTag.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdTag.updateSequenceNum().value()) << ", got "
            << QString::number(accountHighUsn));

    // 7) Create some user's own notes with different USNs

    qevercloud::Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setUpdateSequenceNum(currentUsn++);
    firstNote.setNotebookLocalId(firstNotebook.localId());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    firstNote.setUpdated(firstNote.created());

    qevercloud::Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setUpdateSequenceNum(currentUsn++);
    secondNote.setNotebookLocalId(secondNotebook.localId());
    secondNote.setNotebookGuid(secondNotebook.guid());
    secondNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    secondNote.setUpdated(secondNote.created());

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
        accountHighUsn == secondNote.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(secondNote.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 9) Create one more note, this time with a resource which USN
    // is higher than the note's one

    qevercloud::Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setUpdateSequenceNum(currentUsn++);
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setNotebookGuid(thirdNotebook.guid());
    thirdNote.setNotebookLocalId(thirdNotebook.localId());
    thirdNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setUpdated(thirdNote.created());

    qevercloud::Resource thirdNoteResource;
    thirdNoteResource.setGuid(UidGenerator::Generate());
    thirdNoteResource.setNoteGuid(thirdNote.guid());
    thirdNoteResource.setNoteLocalId(thirdNote.localId());

    thirdNoteResource.setData(qevercloud::Data{});
    thirdNoteResource.mutableData()->setBody(
        QByteArray("Something"));

    thirdNoteResource.mutableData()->setSize(
        thirdNoteResource.data()->body()->size());

    thirdNoteResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *thirdNoteResource.data()->body(),
            QCryptographicHash::Md5));

    thirdNoteResource.setMime(QStringLiteral("text/plain"));
    thirdNoteResource.setUpdateSequenceNum(currentUsn++);

    addNoteResource(thirdNoteResource, thirdNote);

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
        accountHighUsn == thirdNoteResource.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdNoteResource.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 11) Create some user's own saved sarches with different USNs

    qevercloud::SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First search"));
    firstSearch.setUpdateSequenceNum(currentUsn++);
    firstSearch.setQuery(QStringLiteral("First"));

    qevercloud::SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second search"));
    secondSearch.setUpdateSequenceNum(currentUsn++);
    secondSearch.setQuery(QStringLiteral("Second"));

    qevercloud::SavedSearch thirdSearch;
    thirdSearch.setGuid(UidGenerator::Generate());
    thirdSearch.setName(QStringLiteral("Third search"));
    thirdSearch.setUpdateSequenceNum(currentUsn++);
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
        accountHighUsn == thirdSearch.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(thirdSearch.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 13) Create a linked notebook

    qevercloud::LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(UidGenerator::Generate());
    linkedNotebook.setUpdateSequenceNum(currentUsn++);
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
        accountHighUsn == linkedNotebook.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(linkedNotebook.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 15) Add notebook and some tags and notes corresponding to the linked
    // notebook

    qevercloud::Notebook notebookFromLinkedNotebook;
    notebookFromLinkedNotebook.setGuid(linkedNotebook.sharedNotebookGlobalId());

    notebookFromLinkedNotebook.setLinkedNotebookGuid(
        linkedNotebook.guid().value());

    notebookFromLinkedNotebook.setUpdateSequenceNum(currentUsn++);

    notebookFromLinkedNotebook.setName(
        QStringLiteral("Notebook from linked notebook"));

    notebookFromLinkedNotebook.setServiceCreated(
        QDateTime::currentMSecsSinceEpoch());

    notebookFromLinkedNotebook.setServiceUpdated(
        notebookFromLinkedNotebook.serviceCreated());

    qevercloud::Tag firstTagFromLinkedNotebook;
    firstTagFromLinkedNotebook.setGuid(UidGenerator::Generate());

    firstTagFromLinkedNotebook.setName(
        QStringLiteral("First tag from linked notebook"));

    firstTagFromLinkedNotebook.setLinkedNotebookGuid(
        linkedNotebook.guid().value());

    firstTagFromLinkedNotebook.setUpdateSequenceNum(currentUsn++);

    qevercloud::Tag secondTagFromLinkedNotebook;
    secondTagFromLinkedNotebook.setGuid(UidGenerator::Generate());

    secondTagFromLinkedNotebook.setName(
        QStringLiteral("Second tag from linked notebook"));

    secondTagFromLinkedNotebook.setLinkedNotebookGuid(
        linkedNotebook.guid().value());

    secondTagFromLinkedNotebook.setUpdateSequenceNum(currentUsn++);

    qevercloud::Note firstNoteFromLinkedNotebook;
    firstNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    firstNoteFromLinkedNotebook.setUpdateSequenceNum(currentUsn++);

    firstNoteFromLinkedNotebook.setNotebookGuid(
        notebookFromLinkedNotebook.guid());

    firstNoteFromLinkedNotebook.setNotebookLocalId(
        notebookFromLinkedNotebook.localId());

    firstNoteFromLinkedNotebook.setTitle(
        QStringLiteral("First note from linked notebook"));

    firstNoteFromLinkedNotebook.setCreated(
        QDateTime::currentMSecsSinceEpoch());

    firstNoteFromLinkedNotebook.setUpdated(
        firstNoteFromLinkedNotebook.created());

    addNoteTagLocalId(
        firstTagFromLinkedNotebook.localId(), firstNoteFromLinkedNotebook);

    addNoteTagGuid(
        firstTagFromLinkedNotebook.guid().value(),
        firstNoteFromLinkedNotebook);

    qevercloud::Note secondNoteFromLinkedNotebook;
    secondNoteFromLinkedNotebook.setGuid(UidGenerator::Generate());
    secondNoteFromLinkedNotebook.setUpdateSequenceNum(currentUsn++);

    secondNoteFromLinkedNotebook.setNotebookGuid(
        notebookFromLinkedNotebook.guid());

    secondNoteFromLinkedNotebook.setNotebookLocalId(
        notebookFromLinkedNotebook.localId());

    secondNoteFromLinkedNotebook.setTitle(
        QStringLiteral("Second note from linked notebook"));

    secondNoteFromLinkedNotebook.setCreated(
        QDateTime::currentMSecsSinceEpoch());

    secondNoteFromLinkedNotebook.setUpdated(
        secondNoteFromLinkedNotebook.created());

    qevercloud::Resource secondNoteFromLinkedNotebookResource;
    secondNoteFromLinkedNotebookResource.setGuid(UidGenerator::Generate());

    secondNoteFromLinkedNotebookResource.setNoteGuid(
        secondNoteFromLinkedNotebook.guid());

    secondNoteFromLinkedNotebookResource.setNoteLocalId(
        secondNoteFromLinkedNotebook.localId());

    secondNoteFromLinkedNotebookResource.setData(qevercloud::Data{});
    secondNoteFromLinkedNotebookResource.mutableData()->setBody(
        QByteArray("Other something"));

    secondNoteFromLinkedNotebookResource.mutableData()->setSize(
        secondNoteFromLinkedNotebookResource.data()->body()->size());

    secondNoteFromLinkedNotebookResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *secondNoteFromLinkedNotebookResource.data()->body(),
            QCryptographicHash::Md5));

    secondNoteFromLinkedNotebookResource.setUpdateSequenceNum(currentUsn++);

    addNoteResource(
        secondNoteFromLinkedNotebookResource, secondNoteFromLinkedNotebook);

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
        accountHighUsn == linkedNotebook.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(linkedNotebook.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));

    // 17) Verify the current value of the account high USN for the linked
    // notebook

    errorMessage.clear();

    accountHighUsn = localStorageManager.accountHighUsn(
        linkedNotebook.guid().value(), errorMessage);

    QVERIFY2(
        accountHighUsn >= 0, qPrintable(errorMessage.nonLocalizedString()));

    VERIFY2(
        accountHighUsn ==
            secondNoteFromLinkedNotebookResource.updateSequenceNum().value(),
        "Wrong value of account high USN, expected "
            << QString::number(
                   secondNoteFromLinkedNotebookResource.updateSequenceNum().value())
            << ", got " << QString::number(accountHighUsn));
}

void TestAddingNoteWithoutLocalUid()
{
    // 1) Create LocalStorageManager

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"),
        Account::Type::Evernote,
        0};

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Add a notebook in order to test adding notes

    qevercloud::Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 3) Try to add a note without local id without tags or resources
    qevercloud::Note firstNote;
    firstNote.setLocalId(QString{});
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        !firstNote.localId().isEmpty(),
        qPrintable(QStringLiteral(
            "Note local id is empty after LocalStorageManager::addNote method "
            "returning")));

    // 4) Add some tags in order to test adding notes with tags
    qevercloud::Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    qevercloud::Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    qevercloud::Tag thirdTag;
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

    // 5) Try to add a note without local id with tag guids
    qevercloud::Note secondNote;
    secondNote.setLocalId(QString{});
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));

    addNoteTagGuid(firstTag.guid().value(), secondNote);
    addNoteTagGuid(secondTag.guid().value(), secondNote);
    addNoteTagGuid(thirdTag.guid().value(), secondNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 6) Try to add a note without local id with tag guids and with resources
    qevercloud::Note thirdNote;
    thirdNote.setLocalId(QString{});
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(notebook.guid());
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setContent(QStringLiteral("<en-note>third note</en-note>"));

    addNoteTagGuid(firstTag.guid().value(), thirdNote);
    addNoteTagGuid(secondTag.guid().value(), thirdNote);
    addNoteTagGuid(thirdTag.guid().value(), thirdNote);

    qevercloud::Resource resource;
    resource.setGuid(UidGenerator::Generate());
    resource.setNoteGuid(thirdNote.guid());

    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(QByteArray("Data"));
    resource.mutableData()->setSize(resource.data()->body()->size());
    resource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.data()->body(),
            QCryptographicHash::Md5));

    resource.setMime(QStringLiteral("text/plain"));

    addNoteResource(resource, thirdNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(thirdNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));
}

void TestNoteTagIdsComplementWhenAddingAndUpdatingNote()
{
    // 1) Create LocalStorageManager

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    const Account account{
        QStringLiteral("LocalStorageManagerAddNoteWithoutLocalUidTestFakeUser"),
        Account::Type::Evernote,
        0};

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    // 2) Add a notebook in order to test adding notes

    qevercloud::Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("First notebook"));

    QVERIFY2(
        localStorageManager.addNotebook(notebook, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    // 3) Add some tags
    qevercloud::Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First"));

    qevercloud::Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second"));

    qevercloud::Tag thirdTag;
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

    // 4) Add a note without tag local ids but with tag guids
    qevercloud::Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(notebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note>first note</en-note>"));

    addNoteTagGuid(firstTag.guid().value(), firstNote);
    addNoteTagGuid(secondTag.guid().value(), firstNote);
    addNoteTagGuid(thirdTag.guid().value(), firstNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(firstNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const auto tagLocalIds = noteTagLocalIds(firstNote);
    QVERIFY2(
        !tagLocalIds.isEmpty(),
        qPrintable(QStringLiteral("Note has no tag local ids after "
                                  "LocalStorageManager::addNote method "
                                  "returning")));

    QVERIFY2(
        tagLocalIds.size() == 3,
        qPrintable(QStringLiteral(
            "Note's tag local ids have improper size not matching the number "
            "of tag guids after LocalStorageManager::addNote method "
            "returning")));

    QVERIFY2(
        tagLocalIds.contains(firstTag.localId()) &&
            tagLocalIds.contains(secondTag.localId()) &&
            tagLocalIds.contains(thirdTag.localId()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag local ids it should have after "
            "LocalStorageManager::addNote method returning")));

    // 5) Add a note without tag guids but with tag local ids
    qevercloud::Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(notebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note>second note</en-note>"));

    addNoteTagLocalId(firstTag.localId(), secondNote);
    addNoteTagLocalId(secondTag.localId(), secondNote);
    addNoteTagLocalId(thirdTag.localId(), secondNote);

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.addNote(secondNote, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        secondNote.tagGuids() && !secondNote.tagGuids()->isEmpty(),
        qPrintable(QStringLiteral(
            "Note has no tag guids after LocalStorageManager::addNote method "
            "returning")));

    const QStringList & tagGuids = *secondNote.tagGuids();

    QVERIFY2(
        tagGuids.size() == 3,
        qPrintable(QStringLiteral(
            "Note's tag guids have improper size not matching the number of "
            "tag local ids after LocalStorageManager::addNote method "
            "returning")));

    QVERIFY2(
        tagGuids.contains(firstTag.guid().value()) &&
            tagGuids.contains(secondTag.guid().value()) &&
            tagGuids.contains(thirdTag.guid().value()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag guids it should have after "
            "LocalStorageManager::addNote method returning")));

    // 6) Update note with tag guids
    firstNote.setTitle(QStringLiteral("Updated first note"));
    firstNote.setTagLocalIds(QStringList{});
    firstNote.setTagGuids(
        QStringList() << firstTag.guid().value() << secondTag.guid().value());

    errorMessage.clear();

    LocalStorageManager::UpdateNoteOptions updateNoteOptions(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    QVERIFY2(
        localStorageManager.updateNote(
            firstNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    const auto updatedTagLocalIds = noteTagLocalIds(firstNote);

    QVERIFY2(
        !updatedTagLocalIds.isEmpty(),
        qPrintable(QStringLiteral(
            "Note has no tag local ids after LocalStorageManager::updateNote "
            "method returning")));

    QVERIFY2(
        updatedTagLocalIds.size() == 2,
        qPrintable(QStringLiteral(
            "Note's tag local ids have improper size not matching the number "
            "of tag guids after LocalStorageManager::updateNote method "
            "returning")));

    QVERIFY2(
        updatedTagLocalIds.contains(firstTag.localId()) &&
            updatedTagLocalIds.contains(secondTag.localId()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag local ids it should have after "
            "LocalStorageManager::updateNote method returning")));

    // 7) Update note with tag guids
    secondNote.setTitle(QStringLiteral("Updated second note"));
    secondNote.setTagGuids(QStringList());

    secondNote.setTagLocalIds(
        QStringList() << firstTag.localId() << secondTag.localId());

    errorMessage.clear();

    QVERIFY2(
        localStorageManager.updateNote(
            secondNote, updateNoteOptions, errorMessage),
        qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        secondNote.tagGuids() && !secondNote.tagGuids()->isEmpty(),
        qPrintable(QStringLiteral(
            "Note has no tag guids after LocalStorageManager::updateNote "
            "method returning")));

    const QStringList & updatedTagGuids = *secondNote.tagGuids();

    QVERIFY2(
        updatedTagGuids.size() == 2,
        qPrintable(QStringLiteral(
            "Note's tag guids have improper size not matching the number of "
            "tag local ids after LocalStorageManager::updateNote method "
            "returning")));

    QVERIFY2(
        updatedTagGuids.contains(firstTag.guid().value()) &&
            updatedTagGuids.contains(secondTag.guid().value()),
        qPrintable(QStringLiteral(
            "Note doesn't have one of tag guids it should have after "
            "LocalStorageManager::updateNote method returning")));
}

} // namespace test
} // namespace quentier
