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

#include "SpellChecker_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/SuppressWarnings.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QThreadPool>

#include <hunspell/hunspell.hxx>

#include <algorithm>

#define SPELL_CHECKER_FOUND_DICTIONARIES_GROUP                                 \
    QStringLiteral("SpellCheckerFoundDictionaries")

#define SPELL_CHECKER_FOUND_DICTIONARIES_DIC_FILE_ITEM QStringLiteral("DicFile")

#define SPELL_CHECKER_FOUND_DICTIONARIES_AFF_FILE_ITEM QStringLiteral("AffFile")

#define SPELL_CHECKER_FOUND_DICTIONARIES_LANGUAGE_KEY                          \
    QStringLiteral("LanguageKey")

#define SPELL_CHECKER_FOUND_DICTIONARIES_ARRAY QStringLiteral("Dictionaries")

#define SPELL_CHECKER_ENABLED_SYSTEM_DICTIONARIES_KEY                          \
    QStringLiteral("EnabledSystemDictionaries")

namespace quentier {

SpellCheckerPrivate::SpellCheckerPrivate(
    FileIOProcessorAsync * pFileIOProcessorAsync, const Account & account,
    QObject * parent, const QString & userDictionaryPath) :
    QObject(parent),
    m_pFileIOProcessorAsync(pFileIOProcessorAsync), m_currentAccount(account),
    m_pDictionariesFinderStopFlag(new QAtomicInt)
{
    initializeUserDictionary(userDictionaryPath);
    checkAndScanSystemDictionaries();
}

SpellCheckerPrivate::~SpellCheckerPrivate()
{
    Q_UNUSED(m_pDictionariesFinderStopFlag->ref())
}

QVector<std::pair<QString, bool>>
SpellCheckerPrivate::listAvailableDictionaries() const
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::listAvailableDictionaries");

    QVector<std::pair<QString, bool>> result;
    result.reserve(m_systemDictionaries.size());

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const QString & language = it.key();
        const Dictionary & dictionary = it.value();
        result << std::pair<QString, bool>(language, dictionary.m_enabled);
    }

    return result;
}

void SpellCheckerPrivate::setAccount(const Account & account)
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::setAccount: " << account);

    m_currentAccount = account;
    restoreSystemDictionatiesEnabledDisabledSettings();
}

void SpellCheckerPrivate::enableDictionary(const QString & language)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::enableDictionary: language = " << language);

    auto it = m_systemDictionaries.find(language);
    if (it == m_systemDictionaries.end()) {
        QNINFO(
            "note_editor",
            "Can't enable dictionary: no dictionary was "
                << "found for language " << language);
        return;
    }

    it.value().m_enabled = true;
    persistEnabledSystemDictionaries();
}

void SpellCheckerPrivate::disableDictionary(const QString & language)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::disableDictionary: language = " << language);

    auto it = m_systemDictionaries.find(language);
    if (it == m_systemDictionaries.end()) {
        QNINFO(
            "note_editor",
            "Can't disable dictionary: no dictionary was "
                << "found for language " << language);
        return;
    }

    it.value().m_enabled = false;
    persistEnabledSystemDictionaries();
}

bool SpellCheckerPrivate::checkSpell(const QString & word) const
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::checkSpell: " << word);

    if (m_userDictionary.contains(word, Qt::CaseInsensitive)) {
        return true;
    }

    QByteArray wordData = word.toUtf8();
    QByteArray lowerWordData = word.toLower().toUtf8();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            QNTRACE(
                "note_editor",
                "Skipping dictionary " << dictionary.m_dictionaryPath);
            continue;
        }

        bool res = dictionary.m_hunspellWrapper.spell(wordData);
        if (res) {
            QNTRACE(
                "note_editor",
                "Found word " << word << " in dictionary "
                              << dictionary.m_dictionaryPath);
            return true;
        }

        res = dictionary.m_hunspellWrapper.spell(lowerWordData);
        if (res) {
            QNTRACE(
                "note_editor",
                "Found word " << lowerWordData << " in dictionary "
                              << dictionary.m_dictionaryPath);
            return true;
        }
    }

    return false;
}

QStringList SpellCheckerPrivate::spellCorrectionSuggestions(
    const QString & misSpelledWord) const
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::spellCorrectionSuggestions: " << misSpelledWord);

    QByteArray wordData = misSpelledWord.toUtf8();

    QStringList result;
    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            continue;
        }

        result << dictionary.m_hunspellWrapper.suggestions(wordData);
    }

    return result;
}

void SpellCheckerPrivate::addToUserWordlist(const QString & word)
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::addToUserWordlist: " << word);

    ignoreWord(word);

    m_userDictionaryPartPendingWriting << word;
    checkUserDictionaryDataPendingWriting();
}

void SpellCheckerPrivate::removeFromUserWordList(const QString & word)
{
    QNDEBUG(
        "note_editor", "SpellCheckerPrivate::removeFromUserWordList: " << word);

    removeWord(word);

    m_userDictionaryPartPendingWriting.removeAll(word);
    m_userDictionary.removeAll(word);

    // clang-format off
    SAVE_WARNINGS
    CLANG_SUPPRESS_WARNING(-Wrange-loop-analysis)
    // clang-format on
    QByteArray dataToWrite;
    for (const auto it: // clazy:exclude=range-loop
         qevercloud::toRange(qAsConst(m_userDictionary)))
    {
        dataToWrite.append(QString(*it + QStringLiteral("\n")).toUtf8());
    }
    RESTORE_WARNINGS

    QObject::connect(
        this, &SpellCheckerPrivate::writeFile, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onWriteFileRequest);

    QObject::connect(
        m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::writeFileRequestProcessed, this,
        &SpellCheckerPrivate::onWriteFileRequestProcessed);

    m_updateUserDictionaryFileRequestId = QUuid::createUuid();

    Q_EMIT writeFile(
        m_userDictionaryPath, dataToWrite, m_updateUserDictionaryFileRequestId,
        /* append = */ false);

    QNTRACE(
        "note_editor",
        "Sent the request to update the user dictionary: "
            << m_updateUserDictionaryFileRequestId);
}

void SpellCheckerPrivate::ignoreWord(const QString & word)
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::ignoreWord: " << word);

    QByteArray wordData = word.toUtf8();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            continue;
        }

        dictionary.m_hunspellWrapper.add(wordData);
    }
}

void SpellCheckerPrivate::removeWord(const QString & word)
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::removeWord: " << word);

    QByteArray wordData = word.toUtf8();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            continue;
        }

        dictionary.m_hunspellWrapper.remove(wordData);
    }
}

bool SpellCheckerPrivate::isReady() const
{
    return m_systemDictionariesReady && m_userDictionaryReady;
}

void SpellCheckerPrivate::onDictionariesFound(
    SpellCheckerDictionariesFinder::DicAndAffFilesByDictionaryName files)
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::onDictionariesFound");

    for (auto it = files.constBegin(), end = files.constEnd(); it != end; ++it)
    {
        const auto & pair = it.value();
        QNTRACE(
            "note_editor",
            "Raw dictionary file path = "
                << pair.first << ", raw affix file path = " << pair.second);

        Dictionary & dictionary = m_systemDictionaries[it.key()];
        dictionary.m_hunspellWrapper.initialize(pair.second, pair.first);
        dictionary.m_dictionaryPath = pair.first;
        dictionary.m_enabled = true;
        QNTRACE(
            "note_editor",
            "Added dictionary for language " << it.key() << "; dictionary file "
                                             << pair.first << ", affix file "
                                             << pair.second);
    }

    restoreSystemDictionatiesEnabledDisabledSettings();

    ApplicationSettings settings;
    settings.beginGroup(SPELL_CHECKER_FOUND_DICTIONARIES_GROUP);

    settings.beginWriteArray(SPELL_CHECKER_FOUND_DICTIONARIES_ARRAY);
    int index = 0;
    for (auto it = files.constBegin(), end = files.constEnd(); it != end; ++it)
    {
        const auto & pair = it.value();

        settings.setArrayIndex(index);

        settings.setValue(
            SPELL_CHECKER_FOUND_DICTIONARIES_LANGUAGE_KEY, it.key());

        settings.setValue(
            SPELL_CHECKER_FOUND_DICTIONARIES_DIC_FILE_ITEM, pair.first);

        settings.setValue(
            SPELL_CHECKER_FOUND_DICTIONARIES_AFF_FILE_ITEM, pair.second);

        ++index;
    }
    settings.endArray();
    settings.endGroup();

    m_systemDictionariesReady = true;
    if (isReady()) {
        Q_EMIT ready();
    }
}

void SpellCheckerPrivate::checkAndScanSystemDictionaries()
{
    QNDEBUG(
        "note_editor", "SpellCheckerPrivate::checkAndScanSystemDictionaries");

    ApplicationSettings appSettings;
    appSettings.beginGroup(SPELL_CHECKER_FOUND_DICTIONARIES_GROUP);

    m_systemDictionaries.clear();

    SpellCheckerDictionariesFinder::DicAndAffFilesByDictionaryName
        existingDictionaries;

    int size =
        appSettings.beginReadArray(SPELL_CHECKER_FOUND_DICTIONARIES_ARRAY);

    existingDictionaries.reserve(size);
    for (int i = 0; i < size; ++i) {
        appSettings.setArrayIndex(i);

        QString languageKey =
            appSettings.value(SPELL_CHECKER_FOUND_DICTIONARIES_LANGUAGE_KEY)
                .toString();

        if (languageKey.isEmpty()) {
            QNTRACE("note_editor", "No language key, skipping");
            continue;
        }

        QString dicFile =
            appSettings.value(SPELL_CHECKER_FOUND_DICTIONARIES_DIC_FILE_ITEM)
                .toString();

        QFileInfo dicFileInfo(dicFile);
        if (!dicFileInfo.exists() || !dicFileInfo.isReadable()) {
            QNTRACE(
                "note_editor",
                "Skipping non-existing or unreadable dic "
                    << "file: " << dicFileInfo.absoluteFilePath());
            continue;
        }

        QString affFile =
            appSettings.value(SPELL_CHECKER_FOUND_DICTIONARIES_AFF_FILE_ITEM)
                .toString();

        QFileInfo affFileInfo(affFile);
        if (!affFileInfo.exists() || !affFileInfo.isReadable()) {
            QNTRACE(
                "note_editor",
                "Skipping non-existing or unreadable aff "
                    << "file: " << affFileInfo.absoluteFilePath());
            continue;
        }

        existingDictionaries[languageKey] = std::make_pair(dicFile, affFile);
    }

    appSettings.endArray();
    appSettings.endGroup();

    if (existingDictionaries.isEmpty()) {
        QNINFO(
            "note_editor",
            "No previously cached dic/aff files seem to "
                << "actually exist anymore, re-scanning the system for "
                   "dictionaries");
        scanSystemDictionaries();
        return;
    }

    onDictionariesFound(existingDictionaries);
}

void SpellCheckerPrivate::scanSystemDictionaries()
{
    QNDEBUG("note_editor", "SpellCheckerPrivate::scanSystemDictionaries");

    // First try to look for the paths to dictionaries at the environment
    // variables; probably that is the only way to get path to system wide
    // dictionaries on Windows

    QString envVarSeparator;
#ifdef Q_OS_WIN
    envVarSeparator = QStringLiteral(":");
#else
    envVarSeparator = QStringLiteral(";");
#endif

    QString ownDictionaryNames =
        QString::fromLocal8Bit(qgetenv("LIBQUENTIERDICTNAMES"));

    QString ownDictionaryPaths =
        QString::fromLocal8Bit(qgetenv("LIBQUENTIERDICTPATHS"));

    if (!ownDictionaryNames.isEmpty() && !ownDictionaryPaths.isEmpty()) {
        QStringList ownDictionaryNamesList = ownDictionaryNames.split(
            envVarSeparator,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            Qt::SkipEmptyParts,
#else
            QString::SkipEmptyParts,
#endif
            Qt::CaseInsensitive);

        QStringList ownDictionaryPathsList = ownDictionaryPaths.split(
            envVarSeparator,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            Qt::SkipEmptyParts,
#else
            QString::SkipEmptyParts,
#endif
            Qt::CaseInsensitive);

        const int numDictionaries = ownDictionaryNamesList.size();
        if (numDictionaries == ownDictionaryPathsList.size()) {
            for (int i = 0; i < numDictionaries; ++i) {
                QString path = ownDictionaryPathsList[i];
                path = QDir::fromNativeSeparators(path);

                const QString & name = ownDictionaryNamesList[i];

                addSystemDictionary(path, name);
            }
        }
        else {
            QNTRACE(
                "note_editor",
                "Number of found paths to dictionaries "
                    << "doesn't correspond to the number of found dictionary "
                    << "names as deduced from libquentier's own "
                    << "environment variables:\n LIBQUENTIERDICTNAMES: "
                    << ownDictionaryNames
                    << "; \n LIBQUENTIERDICTPATHS: " << ownDictionaryPaths);
        }
    }
    else {
        QNTRACE(
            "note_editor",
            "Can't find LIBQUENTIERDICTNAMES and/or "
                << "LIBQUENTIERDICTPATHS within the environment variables");
    }

    // Also see if there's something set in the environment variables for
    // the hunspell executable itself
    QString hunspellDictionaryName =
        QString::fromLocal8Bit(qgetenv("DICTIONARY"));

    QString hunspellDictionaryPath = QString::fromLocal8Bit(qgetenv("DICPATH"));

    if (!hunspellDictionaryName.isEmpty() && !hunspellDictionaryPath.isEmpty())
    {
        // These environment variables are intended to specify the only one
        // dictionary
        int nameSeparatorIndex =
            hunspellDictionaryName.indexOf(envVarSeparator);
        if (nameSeparatorIndex >= 0) {
            hunspellDictionaryName =
                hunspellDictionaryName.left(nameSeparatorIndex);
        }

        int nameColonIndex =
            hunspellDictionaryName.indexOf(QStringLiteral(","));
        if (nameColonIndex >= 0) {
            hunspellDictionaryName =
                hunspellDictionaryName.left(nameColonIndex);
        }

        hunspellDictionaryName = hunspellDictionaryName.trimmed();

        int pathSeparatorIndex =
            hunspellDictionaryPath.indexOf(envVarSeparator);
        if (pathSeparatorIndex >= 0) {
            hunspellDictionaryPath =
                hunspellDictionaryPath.left(pathSeparatorIndex);
        }

        hunspellDictionaryPath = hunspellDictionaryPath.trimmed();
        hunspellDictionaryPath =
            QDir::fromNativeSeparators(hunspellDictionaryPath);

        addSystemDictionary(hunspellDictionaryPath, hunspellDictionaryName);
    }
    else {
        QNTRACE(
            "note_editor",
            "Can't find DICTIONARY and/or DICPATH within "
                << "the environment variables");
    }

#ifndef Q_OS_WIN
    // Now try to look at standard paths

    QStringList standardPaths;

#ifdef Q_OS_MAC
    standardPaths << QStringLiteral("/Library/Spelling")
                  << QStringLiteral("~/Library/Spelling");
#endif

    standardPaths << QStringLiteral("/usr/share/hunspell");

    QStringList filter;

    // NOTE: look only for ".dic" files, ".aff" ones would be checked separately
    filter << QStringLiteral("*.dic");

    for (const auto & standardPath: qAsConst(standardPaths)) {
        QNTRACE("note_editor", "Inspecting standard path " << standardPath);

        QDir dir(standardPath);
        if (!dir.exists()) {
            QNTRACE(
                "note_editor",
                "Skipping dir " << standardPath << " which doesn't exist");
            continue;
        }

        dir.setNameFilters(filter);
        QFileInfoList fileInfos = dir.entryInfoList(QDir::Files);

        for (const auto & fileInfo: qAsConst(fileInfos)) {
            QString fileName = fileInfo.fileName();
            QNTRACE("note_editor", "Inspecting file name " << fileName);

            if (fileName.endsWith(QStringLiteral(".dic")) ||
                fileName.endsWith(QStringLiteral(".aff")))
            {
                fileName.chop(4); // strip off the ".dic" or ".aff" extension
            }

            addSystemDictionary(standardPath, fileName);
        }
    }

#endif

    if (!m_systemDictionaries.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "Found some dictionaries at the expected "
                << "locations, won't search for dictionaries just everywhere "
                << "at the system");

        restoreSystemDictionatiesEnabledDisabledSettings();

        ApplicationSettings settings;
        settings.beginGroup(SPELL_CHECKER_FOUND_DICTIONARIES_GROUP);

        settings.beginWriteArray(SPELL_CHECKER_FOUND_DICTIONARIES_ARRAY);
        int index = 0;
        for (auto it = m_systemDictionaries.constBegin(),
                  end = m_systemDictionaries.constEnd();
             it != end; ++it)
        {
            settings.setArrayIndex(index);

            const QString & dictionaryName = it.key();
            settings.setValue(
                SPELL_CHECKER_FOUND_DICTIONARIES_LANGUAGE_KEY, dictionaryName);

            const Dictionary & dictionary = it.value();
            QFileInfo dicFileInfo(dictionary.m_dictionaryPath);
            QString dicFilePath = dicFileInfo.absolutePath() +
                QStringLiteral("/") + dictionaryName;

            settings.setValue(
                SPELL_CHECKER_FOUND_DICTIONARIES_DIC_FILE_ITEM,
                dicFilePath + QStringLiteral(".dic"));

            settings.setValue(
                SPELL_CHECKER_FOUND_DICTIONARIES_AFF_FILE_ITEM,
                dicFilePath + QStringLiteral(".aff"));

            ++index;
        }
        settings.endArray();
        settings.endGroup();

        m_systemDictionariesReady = true;
        if (isReady()) {
            Q_EMIT ready();
        }

        return;
    }

    QNDEBUG(
        "note_editor",
        "Can't find hunspell dictionaries in any of "
            << "the expected standard locations, will see if there are some "
            << "previously found dictionaries which are still valid");

    SpellCheckerDictionariesFinder::DicAndAffFilesByDictionaryName
        dicAndAffFiles;
    ApplicationSettings settings;
    QStringList childGroups = settings.childGroups();

    int foundDictionariesGroupIndex =
        childGroups.indexOf(SPELL_CHECKER_FOUND_DICTIONARIES_GROUP);

    if (foundDictionariesGroupIndex >= 0) {
        settings.beginGroup(SPELL_CHECKER_FOUND_DICTIONARIES_GROUP);

        int numDicFiles =
            settings.beginReadArray(SPELL_CHECKER_FOUND_DICTIONARIES_ARRAY);

        dicAndAffFiles.reserve(numDicFiles);
        for (int i = 0; i < numDicFiles; ++i) {
            settings.setArrayIndex(i);

            QString dicFile =
                settings.value(SPELL_CHECKER_FOUND_DICTIONARIES_DIC_FILE_ITEM)
                    .toString();

            QString affFile =
                settings.value(SPELL_CHECKER_FOUND_DICTIONARIES_AFF_FILE_ITEM)
                    .toString();

            if (dicFile.isEmpty() || affFile.isEmpty()) {
                continue;
            }

            QFileInfo dicFileInfo(dicFile);
            if (!dicFileInfo.exists() || !dicFileInfo.isReadable()) {
                continue;
            }

            QFileInfo affFileInfo(affFile);
            if (!affFileInfo.exists() || !affFileInfo.isReadable()) {
                continue;
            }

            dicAndAffFiles[dicFileInfo.baseName()] =
                std::pair<QString, QString>(dicFile, affFile);
        }

        settings.endArray();
        settings.endGroup();
    }

    if (!dicAndAffFiles.isEmpty()) {
        QNDEBUG(
            "note_editor",
            "Found some previously found dictionary files, "
                << "will use them instead of running a new search across the "
                   "system");
        onDictionariesFound(dicAndAffFiles);
        return;
    }

    QNDEBUG(
        "note_editor",
        "Still can't find any valid hunspell dictionaries, "
            << "trying the full recursive search across the entire system, "
               "just to "
            << "find something");

    auto * pFinder =
        new SpellCheckerDictionariesFinder(m_pDictionariesFinderStopFlag);

    QThreadPool::globalInstance()->start(pFinder);

    QObject::connect(
        pFinder, &SpellCheckerDictionariesFinder::foundDictionaries, this,
        &SpellCheckerPrivate::onDictionariesFound, Qt::QueuedConnection);
}

void SpellCheckerPrivate::addSystemDictionary(
    const QString & path, const QString & name)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::addSystemDictionary: path = "
            << path << ", name = " << name);

    QFileInfo dictionaryFileInfo(
        path + QStringLiteral("/") + name + QStringLiteral(".dic"));

    if (!dictionaryFileInfo.exists()) {
        QNTRACE(
            "note_editor",
            "Dictionary file " << dictionaryFileInfo.absoluteFilePath()
                               << " doesn't exist");
        return;
    }

    if (!dictionaryFileInfo.isReadable()) {
        QNTRACE(
            "note_editor",
            "Dictionary file " << dictionaryFileInfo.absoluteFilePath()
                               << " is not readable");
        return;
    }

    QFileInfo affixFileInfo(
        path + QStringLiteral("/") + name + QStringLiteral(".aff"));

    if (!affixFileInfo.exists()) {
        QNTRACE(
            "note_editor",
            "Affix file " << affixFileInfo.absoluteFilePath()
                          << " does not exist");
        return;
    }

    if (!affixFileInfo.isReadable()) {
        QNTRACE(
            "note_editor",
            "Affix file " << affixFileInfo.absoluteFilePath()
                          << " is not readable");
        return;
    }

    QString dictionaryFilePath = dictionaryFileInfo.absoluteFilePath();
    QString affixFilePath = affixFileInfo.absoluteFilePath();
    QNTRACE(
        "note_editor",
        "Raw dictionary file path = " << dictionaryFilePath
                                      << ", raw affix file path = "
                                      << affixFilePath);

    Dictionary & dictionary = m_systemDictionaries[name];
    dictionary.m_hunspellWrapper.initialize(affixFilePath, dictionaryFilePath);
    dictionary.m_dictionaryPath = dictionaryFilePath;
    dictionary.m_enabled = true;
    QNTRACE(
        "note_editor",
        "Added dictionary for language " << name << "; dictionary file "
                                         << dictionaryFilePath
                                         << ", affix file " << affixFilePath);
}

void SpellCheckerPrivate::initializeUserDictionary(
    const QString & userDictionaryPath)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::initializeUserDictionary: "
            << (userDictionaryPath.isEmpty() ? QStringLiteral("<empty>")
                                             : userDictionaryPath));

    bool foundValidPath = false;

    if (!userDictionaryPath.isEmpty()) {
        bool res = checkUserDictionaryPath(userDictionaryPath);
        if (!res) {
            QNINFO(
                "note_editor",
                "Can't accept the proposed user dictionary "
                    << "path, will use the fallback chain of possible user "
                    << "dictionary paths instead");
        }
        else {
            m_userDictionaryPath = userDictionaryPath;
            QNDEBUG(
                "note_editor",
                "Set user dictionary path to " << userDictionaryPath);
            foundValidPath = true;
        }
    }

    if (!foundValidPath) {
        ApplicationSettings settings;
        settings.beginGroup(QStringLiteral("SpellCheck"));

        QString userDictionaryPathFromSettings =
            settings.value(QStringLiteral("UserDictionaryPath")).toString();

        settings.endGroup();

        if (!userDictionaryPathFromSettings.isEmpty()) {
            QNTRACE(
                "note_editor",
                "Inspecting the user dictionary path found "
                    << "in the application settings");
            bool res = checkUserDictionaryPath(userDictionaryPathFromSettings);
            if (!res) {
                QNINFO(
                    "note_editor",
                    "Can't accept the user dictionary path "
                        << "from the application settings: "
                        << userDictionaryPathFromSettings);
            }
            else {
                m_userDictionaryPath = userDictionaryPathFromSettings;
                QNDEBUG(
                    "note_editor",
                    "Set user dictionary path to "
                        << userDictionaryPathFromSettings);
                foundValidPath = true;
            }
        }
    }

    if (!foundValidPath) {
        QNTRACE(
            "note_editor",
            "Haven't found valid user dictionary file path "
                << "within the app settings, fallback to the default path");

        QString fallbackUserDictionaryPath =
            applicationPersistentStoragePath() +
            QStringLiteral("/spellcheck/user_dictionary.txt");
        bool res = checkUserDictionaryPath(fallbackUserDictionaryPath);
        if (!res) {
            QNINFO(
                "note_editor", "Can't accept even the fallback default path");
        }
        else {
            m_userDictionaryPath = fallbackUserDictionaryPath;
            QNDEBUG(
                "note_editor",
                "Set user dictionary path to " << fallbackUserDictionaryPath);
            foundValidPath = true;
        }
    }

    if (foundValidPath) {
        ApplicationSettings settings;
        settings.beginGroup(QStringLiteral("SpellCheck"));

        settings.setValue(
            QStringLiteral("UserDictionaryPath"), m_userDictionaryPath);

        settings.endGroup();

        QObject::connect(
            this, &SpellCheckerPrivate::readFile, m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::onReadFileRequest);

        QObject::connect(
            m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::readFileRequestProcessed, this,
            &SpellCheckerPrivate::onReadFileRequestProcessed);

        m_readUserDictionaryRequestId = QUuid::createUuid();
        Q_EMIT readFile(m_userDictionaryPath, m_readUserDictionaryRequestId);
        QNTRACE(
            "note_editor",
            "Sent the request to read the user dictionary "
                << "file: id = " << m_readUserDictionaryRequestId);
    }
    else {
        QNINFO(
            "note_editor",
            "Please specify the valid path for the user "
                << "dictionary under UserDictionaryPath entry in SpellCheck "
                << "section of application settings");
    }
}

bool SpellCheckerPrivate::checkUserDictionaryPath(
    const QString & userDictionaryPath) const
{
    QFileInfo info(userDictionaryPath);
    if (info.exists()) {
        if (!info.isFile()) {
            QNTRACE(
                "note_editor",
                "User dictionary path candidate is not a "
                    << "file");
            return false;
        }

        if (!info.isReadable() || !info.isWritable()) {
            QFile file(userDictionaryPath);
            bool res = file.setPermissions(QFile::WriteUser | QFile::ReadUser);
            if (!res) {
                QNTRACE(
                    "note_editor",
                    "User dictionary path candidate is a "
                        << "file with insufficient permissions and "
                        << "attempt to fix that has failed: readable ="
                        << (info.isReadable() ? "true" : "false")
                        << ", writable = "
                        << (info.isWritable() ? "true" : "false"));
                return false;
            }
        }

        return true;
    }

    QDir dir = info.absoluteDir();
    if (!dir.exists()) {
        bool res = dir.mkpath(dir.absolutePath());
        if (!res) {
            QNWARNING(
                "note_editor",
                "Can't create not yet existing user "
                    << "dictionary path candidate folder");
            return false;
        }
    }

    return true;
}

void SpellCheckerPrivate::checkUserDictionaryDataPendingWriting()
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate"
            << "::checkUserDictionaryDataPendingWriting");

    if (m_userDictionaryPartPendingWriting.isEmpty()) {
        QNTRACE("note_editor", "Nothing is pending writing");
        return;
    }

    QByteArray dataToWrite;
    for (const auto & part: qAsConst(m_userDictionaryPartPendingWriting)) {
        m_userDictionary << part;
        dataToWrite.append(QString(part + QStringLiteral("\n")).toUtf8());
    }

    if (!dataToWrite.isEmpty()) {
        QObject::connect(
            this, &SpellCheckerPrivate::writeFile, m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::onWriteFileRequest);

        QObject::connect(
            m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::writeFileRequestProcessed, this,
            &SpellCheckerPrivate::onWriteFileRequestProcessed);

        m_appendUserDictionaryPartToFileRequestId = QUuid::createUuid();

        Q_EMIT writeFile(
            m_userDictionaryPath, dataToWrite,
            m_appendUserDictionaryPartToFileRequestId,
            /* append = */ true);

        QNTRACE(
            "note_editor",
            "Sent the request to append the data pending "
                << "writing to user dictionary, id = "
                << m_appendUserDictionaryPartToFileRequestId);
    }

    m_userDictionaryPartPendingWriting.clear();
}

void SpellCheckerPrivate::persistEnabledSystemDictionaries()
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate"
            << "::persistEnabledSystemDictionaries");

    QStringList enabledSystemDictionaries;
    enabledSystemDictionaries.reserve(m_systemDictionaries.size());

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        if (it.value().m_enabled) {
            enabledSystemDictionaries << it.key();
        }
    }

    QNTRACE(
        "note_editor",
        "Enabled system dictionaties: "
            << enabledSystemDictionaries.join(QStringLiteral(", ")));

    ApplicationSettings appSettings(m_currentAccount);
    appSettings.setValue(
        SPELL_CHECKER_ENABLED_SYSTEM_DICTIONARIES_KEY,
        enabledSystemDictionaries);
}

void SpellCheckerPrivate::restoreSystemDictionatiesEnabledDisabledSettings()
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate"
            << "::restoreSystemDictionatiesEnabledDisabledSettings");

    ApplicationSettings appSettings(m_currentAccount);
    bool containsEnabledSystemDictionaries =
        appSettings.contains(SPELL_CHECKER_ENABLED_SYSTEM_DICTIONARIES_KEY);
    QStringList enabledSystemDictionaries =
        appSettings.value(SPELL_CHECKER_ENABLED_SYSTEM_DICTIONARIES_KEY)
            .toStringList();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const QString & name = it.key();

        if (enabledSystemDictionaries.contains(name)) {
            it.value().m_enabled = true;
            QNTRACE("note_editor", "Enabled " << name << " dictionary");
        }
        else {
            it.value().m_enabled = false;
            QNTRACE("note_editor", "Disabled " << name << " dictionary");
        }
    }

    if (containsEnabledSystemDictionaries) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "Found no previously persisted settings for "
            << "enabled system dictionaries, will enable the dictionary "
            << "corresponding to the system locale");

    QLocale systemLocale = QLocale::system();
    QString systemLocaleName = systemLocale.name();
    QNDEBUG("note_editor", "System locale name: " << systemLocaleName);

    auto systemLocaleDictIt = m_systemDictionaries.find(systemLocaleName);
    if (systemLocaleDictIt != m_systemDictionaries.end()) {
        for (auto it = m_systemDictionaries.begin(),
                  end = m_systemDictionaries.end();
             it != end; ++it)
        {
            it.value().m_enabled = (it == systemLocaleDictIt);
        }
    }
    else {
        QNINFO(
            "note_editor",
            "Found no dictionary corresponding to the system "
                << "locale!");

        // Ok, will enable all existing system dictionaries
        for (auto it: qevercloud::toRange(m_systemDictionaries)) {
            it.value().m_enabled = true;
        }
    }

    // Since we had no persistent enabled/disabled dictionaties before,
    // let's persist the default we ended up with
    persistEnabledSystemDictionaries();
}

void SpellCheckerPrivate::onReadFileRequestProcessed(
    bool success, ErrorString errorDescription, QByteArray data,
    QUuid requestId)
{
    Q_UNUSED(errorDescription)

    if (requestId != m_readUserDictionaryRequestId) {
        return;
    }

    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::onReadFileRequestProcessed: "
            << "success = " << (success ? "true" : "false")
            << ", request id = " << requestId);

    m_readUserDictionaryRequestId = QUuid();

    QObject::disconnect(
        this, &SpellCheckerPrivate::readFile, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onReadFileRequest);

    QObject::disconnect(
        m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::readFileRequestProcessed, this,
        &SpellCheckerPrivate::onReadFileRequestProcessed);

    if (Q_LIKELY(success)) {
        QBuffer buffer(&data);
        bool res = buffer.open(QIODevice::ReadOnly);
        if (Q_LIKELY(res)) {
            QTextStream stream(&buffer);
            stream.setCodec("UTF-8");
            QString word;
            while (true) {
                word = stream.readLine();
                if (word.isEmpty()) {
                    break;
                }

                m_userDictionary << word;
            }
            buffer.close();
            checkUserDictionaryDataPendingWriting();
        }
        else {
            QNWARNING("note_editor", "Can't open the data buffer for reading");
        }
    }
    else {
        QNWARNING("note_editor", "Can't read the data from user's dictionary");
    }

    m_userDictionaryReady = true;
    if (isReady()) {
        Q_EMIT ready();
    }
}

void SpellCheckerPrivate::onWriteFileRequestProcessed(
    bool success, ErrorString errorDescription, QUuid requestId)
{
    if (requestId == m_appendUserDictionaryPartToFileRequestId) {
        onAppendUserDictionaryPartDone(success, errorDescription);
    }
    else if (requestId == m_updateUserDictionaryFileRequestId) {
        onUpdateUserDictionaryDone(success, errorDescription);
    }
    else {
        return;
    }

    if (m_appendUserDictionaryPartToFileRequestId.isNull() &&
        m_updateUserDictionaryFileRequestId.isNull())
    {
        QObject::disconnect(
            this, &SpellCheckerPrivate::writeFile, m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::onWriteFileRequest);

        QObject::disconnect(
            m_pFileIOProcessorAsync,
            &FileIOProcessorAsync::writeFileRequestProcessed, this,
            &SpellCheckerPrivate::onWriteFileRequestProcessed);
    }
}

void SpellCheckerPrivate::onAppendUserDictionaryPartDone(
    bool success, ErrorString errorDescription)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate"
            << "::onAppendUserDictionaryPartDone: success = "
            << (success ? "true" : "false"));

    Q_UNUSED(errorDescription)
    m_appendUserDictionaryPartToFileRequestId = QUuid();

    if (Q_UNLIKELY(!success)) {
        QNWARNING(
            "note_editor",
            "Can't append word to the user dictionary "
                << "file");
        return;
    }

    checkUserDictionaryDataPendingWriting();
}

void SpellCheckerPrivate::onUpdateUserDictionaryDone(
    bool success, ErrorString errorDescription)
{
    QNDEBUG(
        "note_editor",
        "SpellCheckerPrivate::onUpdateUserDictionaryDone: "
            << "success = " << (success ? "true" : "false")
            << ", error description = " << errorDescription);

    m_updateUserDictionaryFileRequestId = QUuid();

    if (Q_UNLIKELY(!success)) {
        QNWARNING("note_editor", "Can't update the user dictionary file");
        return;
    }
}

SpellCheckerPrivate::Dictionary::Dictionary() :
    m_hunspellWrapper(), m_dictionaryPath(), m_enabled(true)
{}

bool SpellCheckerPrivate::Dictionary::isEmpty() const
{
    return m_dictionaryPath.isEmpty() || m_hunspellWrapper.isEmpty();
}

void SpellCheckerPrivate::HunspellWrapper::initialize(
    const QString & affFilePath, const QString & dicFilePath)
{
    m_pHunspell = std::make_shared<Hunspell>(
        affFilePath.toLocal8Bit().constData(),
        dicFilePath.toLocal8Bit().constData());
}

bool SpellCheckerPrivate::HunspellWrapper::isEmpty() const
{
    return !m_pHunspell;
}

bool SpellCheckerPrivate::HunspellWrapper::spell(const QString & word) const
{
    return spell(word.toUtf8());
}

bool SpellCheckerPrivate::HunspellWrapper::spell(
    const QByteArray & wordData) const
{
    if (Q_UNLIKELY(!m_pHunspell)) {
        return false;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    return m_pHunspell->spell(wordData.toStdString());
#else
    return m_pHunspell->spell(wordData.constData());
#endif
}

QStringList SpellCheckerPrivate::HunspellWrapper::suggestions(
    const QString & word) const
{
    return suggestions(word.toUtf8());
}

QStringList SpellCheckerPrivate::HunspellWrapper::suggestions(
    const QByteArray & wordData) const
{
    QStringList result;

    if (Q_UNLIKELY(!m_pHunspell)) {
        return result;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    std::vector<std::string> res = m_pHunspell->suggest(wordData.toStdString());

    size_t size = res.size();
    result.reserve(static_cast<int>(
        std::min(size, size_t(std::numeric_limits<int>::max()))));
    for (size_t i = 0; i < size; ++i) {
        result << QString::fromStdString(res[i]);
    }
#else
    char ** rawCorrectionSuggestions = nullptr;
    int numSuggestions =
        m_pHunspell->suggest(&rawCorrectionSuggestions, wordData.constData());
    result.reserve(std::max(numSuggestions, 0));
    for (int i = 0; i < numSuggestions; ++i) {
        QString suggestion = QString::fromUtf8(rawCorrectionSuggestions[i]);
        if (!result.contains(suggestion)) {
            result << suggestion;
        }

        free(rawCorrectionSuggestions[i]);
    }
    free(rawCorrectionSuggestions);
#endif

    return result;
}

void SpellCheckerPrivate::HunspellWrapper::add(const QString & word)
{
#ifdef HUNSPELL_NEW_API_AVAILABLE
    if (Q_UNLIKELY(!m_pHunspell)) {
        return;
    }

    m_pHunspell->add(word.toStdString());
#else
    add(word.toUtf8());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::add(const QByteArray & wordData)
{
    if (Q_UNLIKELY(!m_pHunspell)) {
        return;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    m_pHunspell->add(wordData.toStdString());
#else
    m_pHunspell->add(wordData.constData());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::remove(const QString & word)
{
#ifdef HUNSPELL_NEW_API_AVAILABLE
    if (Q_UNLIKELY(!m_pHunspell)) {
        return;
    }

    m_pHunspell->remove(word.toStdString());
#else
    remove(word.toUtf8());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::remove(const QByteArray & wordData)
{
    if (Q_UNLIKELY(!m_pHunspell)) {
        return;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    m_pHunspell->remove(wordData.toStdString());
#else
    m_pHunspell->remove(wordData.constData());
#endif
}

} // namespace quentier
