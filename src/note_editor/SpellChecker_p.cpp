/*
 * Copyright 2016-2024 Dmitry Ivanov
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
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/StandardPaths.h>

#include <qevercloud/utility/ToRange.h>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>

#include <hunspell/hunspell.hxx>

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace quentier {

using namespace std::string_view_literals;

namespace {

constexpr auto gFoundDictionariesGroupKey = "SpellCheckerFoundDictionaries"sv;
constexpr auto gDicFileKey = "DicFile"sv;
constexpr auto gAffFileKey = "AffFile"sv;
constexpr auto gLanguageKey = "LanguageKey"sv;
constexpr auto gDictionariesKey = "Dictionaries"sv;
constexpr auto gEnabledSystemDictionariesKey = "EnabledSystemDictionaries"sv;

} // namespace

SpellCheckerPrivate::SpellCheckerPrivate(
    FileIOProcessorAsync * fileIOProcessorAsync, Account account,
    QObject * parent, const QString & userDictionaryPath) :
    QObject(parent), m_fileIOProcessorAsync{fileIOProcessorAsync},
    m_currentAccount{std::move(account)}
{
    initializeUserDictionary(userDictionaryPath);
    checkAndScanSystemDictionaries();
}

QList<std::pair<QString, bool>> SpellCheckerPrivate::listAvailableDictionaries()
    const
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::listAvailableDictionaries");

    QList<std::pair<QString, bool>> result;
    result.reserve(m_systemDictionaries.size());

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const QString & language = it.key();
        const Dictionary & dictionary = it.value();
        result << std::pair<QString, bool>(language, dictionary.m_enabled);
    }

    return result;
}

void SpellCheckerPrivate::setAccount(Account account)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::setAccount: " << account);

    m_currentAccount = std::move(account);
    restoreSystemDictionatiesEnabledDisabledSettings();
}

void SpellCheckerPrivate::enableDictionary(const QString & language)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::enableDictionary: language = " << language);

    const auto it = m_systemDictionaries.find(language);
    if (it == m_systemDictionaries.end()) {
        QNINFO(
            "note_editor::SpellCheckerPrivate",
            "Can't enable dictionary: no dictionary was found for language "
                << language);
        return;
    }

    it.value().m_enabled = true;
    persistEnabledSystemDictionaries();
}

void SpellCheckerPrivate::disableDictionary(const QString & language)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::disableDictionary: language = " << language);

    const auto it = m_systemDictionaries.find(language);
    if (it == m_systemDictionaries.end()) {
        QNINFO(
            "note_editor::SpellCheckerPrivate",
            "Can't disable dictionary: no dictionary was found for language "
                << language);
        return;
    }

    it.value().m_enabled = false;
    persistEnabledSystemDictionaries();
}

bool SpellCheckerPrivate::checkSpell(const QString & word) const
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::checkSpell: " << word);

    if (m_userDictionary.contains(word, Qt::CaseInsensitive)) {
        return true;
    }

    const QByteArray wordData = word.toUtf8();
    const QByteArray lowerWordData = word.toLower().toUtf8();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Skipping dictionary " << dictionary.m_dictionaryPath);
            continue;
        }

        if (dictionary.m_hunspellWrapper.spell(wordData)) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Found word " << word << " in dictionary "
                              << dictionary.m_dictionaryPath);
            return true;
        }

        if (dictionary.m_hunspellWrapper.spell(lowerWordData)) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
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
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::spellCorrectionSuggestions: " << misSpelledWord);

    const QByteArray wordData = misSpelledWord.toUtf8();

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
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::addToUserWordlist: " << word);

    ignoreWord(word);

    m_userDictionaryPartPendingWriting << word;
    checkUserDictionaryDataPendingWriting();
}

void SpellCheckerPrivate::removeFromUserWordList(const QString & word)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::removeFromUserWordList: " << word);

    removeWord(word);

    m_userDictionaryPartPendingWriting.removeAll(word);
    m_userDictionary.removeAll(word);

    QByteArray dataToWrite;
    for (auto it = m_userDictionary.constBegin(),
              end = m_userDictionary.constEnd();
         it != end; ++it)
    {
        dataToWrite.append(QString(*it + QStringLiteral("\n")).toUtf8());
    }

    QObject::connect(
        this, &SpellCheckerPrivate::writeFile, m_fileIOProcessorAsync,
        &FileIOProcessorAsync::onWriteFileRequest);

    QObject::connect(
        m_fileIOProcessorAsync,
        &FileIOProcessorAsync::writeFileRequestProcessed, this,
        &SpellCheckerPrivate::onWriteFileRequestProcessed);

    m_updateUserDictionaryFileRequestId = QUuid::createUuid();

    Q_EMIT writeFile(
        m_userDictionaryPath, dataToWrite, m_updateUserDictionaryFileRequestId,
        /* append = */ false);

    QNTRACE(
        "note_editor::SpellCheckerPrivate",
        "Sent the request to update the user dictionary: "
            << m_updateUserDictionaryFileRequestId);
}

void SpellCheckerPrivate::ignoreWord(const QString & word)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::ignoreWord: " << word);

    const QByteArray wordData = word.toUtf8();

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
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::removeWord: " << word);

    QByteArray wordData = word.toUtf8();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        Dictionary & dictionary = it.value();

        if (dictionary.isEmpty() || !dictionary.m_enabled) {
            continue;
        }

        dictionary.m_hunspellWrapper.remove(wordData);
    }
}

bool SpellCheckerPrivate::isReady() const noexcept
{
    return m_systemDictionariesReady && m_userDictionaryReady;
}

void SpellCheckerPrivate::persistFoundDictionariesData(
    const DictionariesByName & dictionaries)
{
    if (QuentierIsLogLevelActive(LogLevel::Debug)) {
        QString dictionariesStr;
        QTextStream strm{&dictionariesStr};
        for (const auto it: qevercloud::toRange(dictionaries)) {
            strm << "  [dic file: " << it->dicFile
                 << ", aff file: " << it->affFile << "];\n";
        }

        QNDEBUG(
            "note_editor::SpellCheckerPrivate",
            "SpellCheckerPrivate::persistFoundDictionariesData: "
                << dictionaries.size() << " dictionaries:\n"
                << dictionariesStr);
    }

    for (auto it = dictionaries.constBegin(), end = dictionaries.constEnd();
         it != end; ++it)
    {
        const auto & dictionaryData = it.value();
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Raw dictionary file path = " << dictionaryData.dicFile
                                          << ", raw affix file path = "
                                          << dictionaryData.affFile);

        Dictionary & dictionary = m_systemDictionaries[it.key()];
        dictionary.m_hunspellWrapper.initialize(
            dictionaryData.affFile, dictionaryData.dicFile);
        dictionary.m_dictionaryPath = dictionaryData.dicFile;
        dictionary.m_enabled = true;
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Added dictionary for language "
                << it.key() << "; dictionary file " << dictionaryData.dicFile
                << ", affix file " << dictionaryData.affFile);
    }

    restoreSystemDictionatiesEnabledDisabledSettings();

    ApplicationSettings settings;
    settings.beginGroup(gFoundDictionariesGroupKey);

    settings.beginWriteArray(gDictionariesKey);
    int index = 0;
    for (auto it = dictionaries.constBegin(), end = dictionaries.constEnd();
         it != end; ++it)
    {
        const auto & dictionaryData = it.value();
        settings.setArrayIndex(index);

        settings.setValue(gLanguageKey, it.key());
        settings.setValue(gDicFileKey, dictionaryData.dicFile);
        settings.setValue(gAffFileKey, dictionaryData.affFile);

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
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::checkAndScanSystemDictionaries");

    ApplicationSettings appSettings;
    appSettings.beginGroup(gFoundDictionariesGroupKey);

    m_systemDictionaries.clear();

    DictionariesByName systemDictionaries;

    const int size = appSettings.beginReadArray(gDictionariesKey);

    systemDictionaries.reserve(size);
    for (int i = 0; i < size; ++i) {
        appSettings.setArrayIndex(i);

        const QString languageKey = appSettings.value(gLanguageKey).toString();
        if (languageKey.isEmpty()) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "No language key, skipping");
            continue;
        }

        QString dicFile = appSettings.value(gDicFileKey).toString();

        const QFileInfo dicFileInfo(dicFile);
        if (!dicFileInfo.exists() || !dicFileInfo.isReadable()) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Skipping non-existing or unreadable dic file: "
                    << dicFileInfo.absoluteFilePath());
            continue;
        }

        QString affFile = appSettings.value(gAffFileKey).toString();

        const QFileInfo affFileInfo{affFile};
        if (!affFileInfo.exists() || !affFileInfo.isReadable()) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Skipping non-existing or unreadable aff file: "
                    << affFileInfo.absoluteFilePath());
            continue;
        }

        systemDictionaries[languageKey] =
            HunspellDictionaryData{std::move(dicFile), std::move(affFile)};
    }

    appSettings.endArray();
    appSettings.endGroup();

    if (systemDictionaries.isEmpty()) {
        QNINFO(
            "note_editor::SpellCheckerPrivate",
            "No previously cached dic/aff files seem to actually exist "
                << "anymore, re-scanning the system for dictionaries");
        scanSystemDictionaries();
        return;
    }

    persistFoundDictionariesData(systemDictionaries);
}

void SpellCheckerPrivate::scanSystemDictionaries()
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::scanSystemDictionaries");

    // First try to look for the paths to dictionaries at the environment
    // variables; probably that is the only way to get path to system wide
    // dictionaries on Windows

    const QString envVarSeparator =
#ifdef Q_OS_WIN
        QStringLiteral(":");
#else
        QStringLiteral(";");
#endif

    const QString ownDictionaryNames =
        QString::fromLocal8Bit(qgetenv("LIBQUENTIERDICTNAMES"));

    const QString ownDictionaryPaths =
        QString::fromLocal8Bit(qgetenv("LIBQUENTIERDICTPATHS"));

    if (!ownDictionaryNames.isEmpty() && !ownDictionaryPaths.isEmpty()) {
        const QStringList ownDictionaryNamesList = ownDictionaryNames.split(
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

        const auto numDictionaries = ownDictionaryNamesList.size();
        if (numDictionaries == ownDictionaryPathsList.size()) {
            // FIXME: change to qsizetype after full migration to Qt6
            for (int i = 0; i < numDictionaries; ++i) {
                addSystemDictionary(
                    QDir::fromNativeSeparators(ownDictionaryPathsList[i]),
                    ownDictionaryNamesList[i]);
            }
        }
        else {
            QNWARNING(
                "note_editor::SpellCheckerPrivate",
                "Number of found paths to dictionaries doesn't correspond to "
                    << "the number of found dictionary names as deduced from "
                    << "libquentier's own environment variables:\n "
                    << "LIBQUENTIERDICTNAMES: " << ownDictionaryNames << "; \n"
                    << "LIBQUENTIERDICTPATHS: " << ownDictionaryPaths);
        }
    }
    else {
        QNDEBUG(
            "note_editor::SpellCheckerPrivate",
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
        const auto nameSeparatorIndex =
            hunspellDictionaryName.indexOf(envVarSeparator);

        if (nameSeparatorIndex >= 0) {
            hunspellDictionaryName =
                hunspellDictionaryName.left(nameSeparatorIndex);
        }

        const auto nameColonIndex =
            hunspellDictionaryName.indexOf(QStringLiteral(","));

        if (nameColonIndex >= 0) {
            hunspellDictionaryName =
                hunspellDictionaryName.left(nameColonIndex);
        }

        hunspellDictionaryName = hunspellDictionaryName.trimmed();

        const auto pathSeparatorIndex =
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
            "note_editor::SpellCheckerPrivate",
            "Can't find DICTIONARY and/or DICPATH within the env variables");
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

    for (const auto & standardPath: std::as_const(standardPaths)) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Inspecting standard path " << standardPath);

        QDir dir{standardPath};
        if (!dir.exists()) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Skipping dir " << standardPath << " which doesn't exist");
            continue;
        }

        dir.setNameFilters(filter);

        const QFileInfoList fileInfos = dir.entryInfoList(QDir::Files);
        for (const auto & fileInfo: std::as_const(fileInfos)) {
            QString fileName = fileInfo.fileName();
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Inspecting file name " << fileName);

            if (fileName.endsWith(QStringLiteral(".dic")) ||
                fileName.endsWith(QStringLiteral(".aff")))
            {
                fileName.chop(4); // strip off the ".dic" or ".aff" extension
            }

            addSystemDictionary(standardPath, fileName);
        }
    }

#endif // Q_OS_WIN

    if (!m_systemDictionaries.isEmpty()) {
        QNDEBUG(
            "note_editor::SpellCheckerPrivate",
            "Found some system dictionaries in standard locations");

        restoreSystemDictionatiesEnabledDisabledSettings();

        ApplicationSettings settings;
        settings.beginGroup(gFoundDictionariesGroupKey);

        settings.beginWriteArray(gDictionariesKey);
        int index = 0;
        for (auto it = m_systemDictionaries.constBegin(),
                  end = m_systemDictionaries.constEnd();
             it != end; ++it)
        {
            settings.setArrayIndex(index);

            const QString & dictionaryName = it.key();
            settings.setValue(gLanguageKey, dictionaryName);

            const Dictionary & dictionary = it.value();
            QFileInfo dicFileInfo(dictionary.m_dictionaryPath);
            QString dicFilePath = dicFileInfo.absolutePath() +
                QStringLiteral("/") + dictionaryName;

            settings.setValue(
                gDicFileKey, dicFilePath + QStringLiteral(".dic"));

            settings.setValue(
                gAffFileKey, dicFilePath + QStringLiteral(".aff"));

            ++index;
        }
        settings.endArray();
        settings.endGroup();
    }

    m_systemDictionariesReady = true;
    if (isReady()) {
        Q_EMIT ready();
    }
}

void SpellCheckerPrivate::addSystemDictionary(
    const QString & path, const QString & name)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::addSystemDictionary: path = "
            << path << ", name = " << name);

    const QFileInfo dictionaryFileInfo(
        path + QStringLiteral("/") + name + QStringLiteral(".dic"));

    if (!dictionaryFileInfo.exists()) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Dictionary file " << dictionaryFileInfo.absoluteFilePath()
                               << " doesn't exist");
        return;
    }

    if (!dictionaryFileInfo.isReadable()) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Dictionary file " << dictionaryFileInfo.absoluteFilePath()
                               << " is not readable");
        return;
    }

    const QFileInfo affixFileInfo(
        path + QStringLiteral("/") + name + QStringLiteral(".aff"));

    if (!affixFileInfo.exists()) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Affix file " << affixFileInfo.absoluteFilePath()
                          << " does not exist");
        return;
    }

    if (!affixFileInfo.isReadable()) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Affix file " << affixFileInfo.absoluteFilePath()
                          << " is not readable");
        return;
    }

    const QString dictionaryFilePath = dictionaryFileInfo.absoluteFilePath();
    const QString affixFilePath = affixFileInfo.absoluteFilePath();

    QNTRACE(
        "note_editor::SpellCheckerPrivate",
        "Raw dictionary file path = " << dictionaryFilePath
                                      << ", raw affix file path = "
                                      << affixFilePath);

    Dictionary & dictionary = m_systemDictionaries[name];
    dictionary.m_hunspellWrapper.initialize(affixFilePath, dictionaryFilePath);
    dictionary.m_dictionaryPath = dictionaryFilePath;
    dictionary.m_enabled = true;

    QNTRACE(
        "note_editor::SpellCheckerPrivate",
        "Added dictionary for language " << name << "; dictionary file "
                                         << dictionaryFilePath
                                         << ", affix file " << affixFilePath);
}

void SpellCheckerPrivate::initializeUserDictionary(
    const QString & userDictionaryPath)
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::initializeUserDictionary: "
            << (userDictionaryPath.isEmpty() ? QStringLiteral("<empty>")
                                             : userDictionaryPath));

    bool foundValidPath = false;

    if (!userDictionaryPath.isEmpty()) {
        if (!checkUserDictionaryPath(userDictionaryPath)) {
            QNINFO(
                "note_editor::SpellCheckerPrivate",
                "Can't accept the proposed user dictionary path, will use the "
                "fallback chain of possible user dictionary paths instead");
        }
        else {
            m_userDictionaryPath = userDictionaryPath;
            QNDEBUG(
                "note_editor::SpellCheckerPrivate",
                "Set user dictionary path to "
                    << QDir::toNativeSeparators(userDictionaryPath));
            foundValidPath = true;
        }
    }

    if (!foundValidPath) {
        ApplicationSettings settings;
        settings.beginGroup("SpellCheck"sv);

        const QString userDictionaryPathFromSettings =
            settings.value("UserDictionaryPath"sv).toString();

        settings.endGroup();

        if (!userDictionaryPathFromSettings.isEmpty()) {
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Inspecting user dictionary path from application settings");
            if (!checkUserDictionaryPath(userDictionaryPathFromSettings)) {
                QNINFO(
                    "note_editor::SpellCheckerPrivate",
                    "Can't accept user dictionary path from application "
                        << "settings: "
                        << QDir::toNativeSeparators(
                               userDictionaryPathFromSettings));
            }
            else {
                m_userDictionaryPath = userDictionaryPathFromSettings;
                QNDEBUG(
                    "note_editor::SpellCheckerPrivate",
                    "Set user dictionary path to " << QDir::toNativeSeparators(
                        userDictionaryPathFromSettings));
                foundValidPath = true;
            }
        }
    }

    if (!foundValidPath) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Haven't found valid user dictionary file path within the "
            "settings, fallback to the default path");

        const QString fallbackUserDictionaryPath =
            applicationPersistentStoragePath() +
            QStringLiteral("/spellcheck/user_dictionary.txt");

        if (!checkUserDictionaryPath(fallbackUserDictionaryPath)) {
            QNINFO(
                "note_editor::SpellCheckerPrivate",
                "Can't accept the fallback default path: "
                    << QDir::toNativeSeparators(fallbackUserDictionaryPath));
        }
        else {
            m_userDictionaryPath = fallbackUserDictionaryPath;
            QNDEBUG(
                "note_editor::SpellCheckerPrivate",
                "Set user dictionary path to "
                    << QDir::toNativeSeparators(fallbackUserDictionaryPath));
            foundValidPath = true;
        }
    }

    if (foundValidPath) {
        ApplicationSettings settings;
        settings.beginGroup("SpellCheck"sv);
        settings.setValue("UserDictionaryPath"sv, m_userDictionaryPath);
        settings.endGroup();

        QObject::connect(
            this, &SpellCheckerPrivate::readFile, m_fileIOProcessorAsync,
            &FileIOProcessorAsync::onReadFileRequest);

        QObject::connect(
            m_fileIOProcessorAsync,
            &FileIOProcessorAsync::readFileRequestProcessed, this,
            &SpellCheckerPrivate::onReadFileRequestProcessed);

        m_readUserDictionaryRequestId = QUuid::createUuid();

        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Sending the request to read user dictionary file: request id = "
                << m_readUserDictionaryRequestId);

        Q_EMIT readFile(m_userDictionaryPath, m_readUserDictionaryRequestId);
    }
    else {
        QNINFO(
            "note_editor::SpellCheckerPrivate",
            "No valid path for user dictionary under UserDictionaryPath entry "
                << "in SpellCheck section of application settings");
    }
}

bool SpellCheckerPrivate::checkUserDictionaryPath(
    const QString & userDictionaryPath) const
{
    const QFileInfo info{userDictionaryPath};
    if (info.exists()) {
        if (!info.isFile()) {
            QNINFO(
                "note_editor::SpellCheckerPrivate",
                "User dictionary path candidate is not a file: "
                    << QDir::toNativeSeparators(userDictionaryPath));
            return false;
        }

        if (!info.isReadable() || !info.isWritable()) {
            QFile file{userDictionaryPath};
            if (!file.setPermissions(QFile::WriteUser | QFile::ReadUser)) {
                QNINFO(
                    "note_editor::SpellCheckerPrivate",
                    "User dictionary path candidate is a file with "
                        << "insufficient permissions and attempt to fix that "
                        << "has failed: readable ="
                        << (info.isReadable() ? "true" : "false")
                        << ", writable = "
                        << (info.isWritable() ? "true" : "false") << ", path = "
                        << QDir::toNativeSeparators(userDictionaryPath));
                return false;
            }
        }

        return true;
    }

    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
        QNWARNING(
            "note_editor::SpellCheckerPrivate",
            "Can't create not yet existing user dictionary path candidate "
                << "folder");
        return false;
    }

    return true;
}

void SpellCheckerPrivate::checkUserDictionaryDataPendingWriting()
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::checkUserDictionaryDataPendingWriting");

    if (m_userDictionaryPartPendingWriting.isEmpty()) {
        QNTRACE(
            "note_editor::SpellCheckerPrivate", "Nothing is pending writing");
        return;
    }

    QByteArray dataToWrite;
    for (const auto & part: std::as_const(m_userDictionaryPartPendingWriting)) {
        m_userDictionary << part;
        dataToWrite.append(QString{part + QStringLiteral("\n")}.toUtf8());
    }

    if (!dataToWrite.isEmpty()) {
        QObject::connect(
            this, &SpellCheckerPrivate::writeFile, m_fileIOProcessorAsync,
            &FileIOProcessorAsync::onWriteFileRequest);

        QObject::connect(
            m_fileIOProcessorAsync,
            &FileIOProcessorAsync::writeFileRequestProcessed, this,
            &SpellCheckerPrivate::onWriteFileRequestProcessed);

        m_appendUserDictionaryPartToFileRequestId = QUuid::createUuid();

        QNTRACE(
            "note_editor::SpellCheckerPrivate",
            "Sending the request to append the data pending writing to user "
                << "dictionary, id = "
                << m_appendUserDictionaryPartToFileRequestId);

        Q_EMIT writeFile(
            m_userDictionaryPath, dataToWrite,
            m_appendUserDictionaryPartToFileRequestId,
            /* append = */ true);
    }

    m_userDictionaryPartPendingWriting.clear();
}

void SpellCheckerPrivate::persistEnabledSystemDictionaries()
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::persistEnabledSystemDictionaries");

    QStringList enabledSystemDictionaries;
    enabledSystemDictionaries.reserve(m_systemDictionaries.size());

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        if (it.value().m_enabled) {
            enabledSystemDictionaries << it.key();
        }
    }

    QNTRACE(
        "note_editor::SpellCheckerPrivate",
        "Enabled system dictionaties: "
            << enabledSystemDictionaries.join(QStringLiteral(", ")));

    ApplicationSettings appSettings{m_currentAccount};
    appSettings.setValue(
        gEnabledSystemDictionariesKey, enabledSystemDictionaries);
}

void SpellCheckerPrivate::restoreSystemDictionatiesEnabledDisabledSettings()
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate"
            << "::restoreSystemDictionatiesEnabledDisabledSettings");

    ApplicationSettings appSettings{m_currentAccount};

    const bool containsEnabledSystemDictionaries =
        appSettings.contains(gEnabledSystemDictionariesKey);

    const QStringList enabledSystemDictionaries =
        appSettings.value(gEnabledSystemDictionariesKey).toStringList();

    for (const auto it: qevercloud::toRange(m_systemDictionaries)) {
        const QString & name = it.key();

        if (enabledSystemDictionaries.contains(name)) {
            it.value().m_enabled = true;
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Enabled " << name << " dictionary");
        }
        else {
            it.value().m_enabled = false;
            QNTRACE(
                "note_editor::SpellCheckerPrivate",
                "Disabled " << name << " dictionary");
        }
    }

    if (containsEnabledSystemDictionaries) {
        return;
    }

    const QLocale systemLocale = QLocale::system();
    const QString systemLocaleName = systemLocale.name();

    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "Found no previously persisted settings for enabled system "
            << "dictionaries, will enable the dictionary corresponding to the "
            << "system locale: " << systemLocaleName);

    const auto systemLocaleDictIt = m_systemDictionaries.find(systemLocaleName);
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
            "note_editor::SpellCheckerPrivate",
            "Found no dictionary corresponding to the system locale!");

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
    bool success, ErrorString errorDescription, // NOLINT
    QByteArray data, QUuid requestId)
{
    Q_UNUSED(errorDescription)

    if (requestId != m_readUserDictionaryRequestId) {
        return;
    }

    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::onReadFileRequestProcessed: "
            << "success = " << (success ? "true" : "false")
            << ", request id = " << requestId);

    m_readUserDictionaryRequestId = QUuid();

    QObject::disconnect(
        this, &SpellCheckerPrivate::readFile, m_fileIOProcessorAsync,
        &FileIOProcessorAsync::onReadFileRequest);

    QObject::disconnect(
        m_fileIOProcessorAsync, &FileIOProcessorAsync::readFileRequestProcessed,
        this, &SpellCheckerPrivate::onReadFileRequestProcessed);

    if (Q_LIKELY(success)) {
        QBuffer buffer{&data};
        if (buffer.open(QIODevice::ReadOnly)) {
            QTextStream stream{&buffer};

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            stream.setCodec("UTF-8");
#endif

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
            QNWARNING(
                "note_editor::SpellCheckerPrivate",
                "Can't open the data buffer for reading");
        }
    }
    else {
        QNWARNING(
            "note_editor::SpellCheckerPrivate",
            "Can't read the data from user's dictionary");
    }

    m_userDictionaryReady = true;
    if (isReady()) {
        Q_EMIT ready();
    }
}

void SpellCheckerPrivate::onWriteFileRequestProcessed(
    bool success, ErrorString errorDescription, QUuid requestId) // NOLINT
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
            this, &SpellCheckerPrivate::writeFile, m_fileIOProcessorAsync,
            &FileIOProcessorAsync::onWriteFileRequest);

        QObject::disconnect(
            m_fileIOProcessorAsync,
            &FileIOProcessorAsync::writeFileRequestProcessed, this,
            &SpellCheckerPrivate::onWriteFileRequestProcessed);
    }
}

void SpellCheckerPrivate::onAppendUserDictionaryPartDone(
    bool success, ErrorString errorDescription) // NOLINT
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate" << "::onAppendUserDictionaryPartDone: success = "
                              << (success ? "true" : "false"));

    Q_UNUSED(errorDescription)
    m_appendUserDictionaryPartToFileRequestId = QUuid{};

    if (Q_UNLIKELY(!success)) {
        QNWARNING(
            "note_editor::SpellCheckerPrivate",
            "Can't append word to the user dictionary file");
        return;
    }

    checkUserDictionaryDataPendingWriting();
}

void SpellCheckerPrivate::onUpdateUserDictionaryDone(
    bool success, ErrorString errorDescription) // NOLINT
{
    QNDEBUG(
        "note_editor::SpellCheckerPrivate",
        "SpellCheckerPrivate::onUpdateUserDictionaryDone: "
            << "success = " << (success ? "true" : "false")
            << ", error description = " << errorDescription);

    m_updateUserDictionaryFileRequestId = QUuid();

    if (Q_UNLIKELY(!success)) {
        QNWARNING(
            "note_editor::SpellCheckerPrivate",
            "Can't update the user dictionary file");
        return;
    }
}

bool SpellCheckerPrivate::Dictionary::isEmpty() const noexcept
{
    return m_dictionaryPath.isEmpty() || m_hunspellWrapper.isEmpty();
}

void SpellCheckerPrivate::HunspellWrapper::initialize(
    const QString & affFilePath, const QString & dicFilePath)
{
#ifdef Q_OS_WIN
    // On Windows things are a little more difficult than on other platforms:
    // First, paths need to be converted to native separators
    // Second, Hunspell recommends to use UTF-8 encoded paths started with
    // long path prefix
    const QByteArray prefix{"\\\\?\\"};
    QByteArray affFilePathUtf8 = prefix + affFilePath.toUtf8();
    QByteArray dicFilePathUtf8 = prefix + dicFilePath.toUtf8();
    m_hunspell = std::make_shared<Hunspell>(
        affFilePathUtf8.constData(), dicFilePathUtf8.constData());
#else
    m_hunspell = std::make_shared<Hunspell>(
        affFilePath.toUtf8().constData(), dicFilePath.toUtf8().constData());
#endif
}

bool SpellCheckerPrivate::HunspellWrapper::isEmpty() const noexcept
{
    return !m_hunspell;
}

bool SpellCheckerPrivate::HunspellWrapper::spell(const QString & word) const
{
    return spell(word.toUtf8());
}

bool SpellCheckerPrivate::HunspellWrapper::spell(
    const QByteArray & wordData) const
{
    if (Q_UNLIKELY(!m_hunspell)) {
        return false;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    return m_hunspell->spell(wordData.toStdString());
#else
    return m_hunspell->spell(wordData.constData());
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

    if (Q_UNLIKELY(!m_hunspell)) {
        return result;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    const std::vector<std::string> res =
        m_hunspell->suggest(wordData.toStdString());

    const std::size_t size = res.size();
    result.reserve(static_cast<int>(
        std::min(size, std::size_t(std::numeric_limits<int>::max()))));

    for (std::size_t i = 0; i < size; ++i) {
        result << QString::fromStdString(res[i]);
    }
#else
    char ** rawCorrectionSuggestions = nullptr;

    const int numSuggestions =
        m_hunspell->suggest(&rawCorrectionSuggestions, wordData.constData());

    result.reserve(std::max(numSuggestions, 0));
    for (int i = 0; i < numSuggestions; ++i) {
        const QString suggestion =
            QString::fromUtf8(rawCorrectionSuggestions[i]);

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
    if (Q_UNLIKELY(!m_hunspell)) {
        return;
    }

    m_hunspell->add(word.toStdString());
#else
    add(word.toUtf8());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::add(const QByteArray & wordData)
{
    if (Q_UNLIKELY(!m_hunspell)) {
        return;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    m_hunspell->add(wordData.toStdString());
#else
    m_hunspell->add(wordData.constData());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::remove(const QString & word)
{
#ifdef HUNSPELL_NEW_API_AVAILABLE
    if (Q_UNLIKELY(!m_hunspell)) {
        return;
    }

    m_hunspell->remove(word.toStdString());
#else
    remove(word.toUtf8());
#endif
}

void SpellCheckerPrivate::HunspellWrapper::remove(const QByteArray & wordData)
{
    if (Q_UNLIKELY(!m_hunspell)) {
        return;
    }

#ifdef HUNSPELL_NEW_API_AVAILABLE
    m_hunspell->remove(wordData.toStdString());
#else
    m_hunspell->remove(wordData.constData());
#endif
}

} // namespace quentier
