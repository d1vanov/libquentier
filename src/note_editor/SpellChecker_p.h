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

#pragma once

#include "SpellCheckerDictionariesFinder.h"

#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>

#include <QAtomicInt>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <memory>
#include <utility>

class Hunspell;

namespace quentier {

class FileIOProcessorAsync;

class SpellCheckerPrivate final : public QObject
{
    Q_OBJECT
public:
    SpellCheckerPrivate(
        FileIOProcessorAsync * pFileIOProcessorAsync, Account account,
        QObject * parent = nullptr, const QString & userDictionaryPath = {});

    ~SpellCheckerPrivate() override;

    // The second bool in the pair indicates whether the dictionary
    // is enabled or disabled
    [[nodiscard]] QVector<std::pair<QString, bool>> listAvailableDictionaries()
        const;

    void setAccount(const Account & account);

    void enableDictionary(const QString & language);
    void disableDictionary(const QString & language);

    [[nodiscard]] bool checkSpell(const QString & word) const;

    [[nodiscard]] QStringList spellCorrectionSuggestions(
        const QString & misSpelledWord) const;

    void addToUserWordlist(const QString & word);
    void removeFromUserWordList(const QString & word);
    void ignoreWord(const QString & word);
    void removeWord(const QString & word);

    [[nodiscard]] bool isReady() const noexcept;

Q_SIGNALS:
    void ready();

    // private signals
    void readFile(QString absoluteFilePath, QUuid requestId);

    void writeFile(
        QString absoluteFilePath, QByteArray data, QUuid requestId,
        bool append);

private Q_SLOTS:
    void onDictionariesFound(
        SpellCheckerDictionariesFinder::DicAndAffFilesByDictionaryName files);

private:
    void checkAndScanSystemDictionaries();
    void scanSystemDictionaries();
    void addSystemDictionary(const QString & path, const QString & name);

    void initializeUserDictionary(const QString & userDictionaryPath);

    [[nodiscard]] bool checkUserDictionaryPath(
        const QString & userDictionaryPath) const;

    void checkUserDictionaryDataPendingWriting();

    void onAppendUserDictionaryPartDone(
        bool success, ErrorString errorDescription);

    void onUpdateUserDictionaryDone(bool success, ErrorString errorDescription);

    void persistEnabledSystemDictionaries();
    void restoreSystemDictionatiesEnabledDisabledSettings();

private Q_SLOTS:
    void onReadFileRequestProcessed(
        bool success, ErrorString errorDescription, QByteArray data,
        QUuid requestId);

    void onWriteFileRequestProcessed(
        bool success, ErrorString errorDescription, QUuid requestId);

private:
    class HunspellWrapper
    {
    public:
        void initialize(
            const QString & affFilePath, const QString & dicFilePath);

        [[nodiscard]] bool isEmpty() const noexcept;

        [[nodiscard]] bool spell(const QString & word) const;
        [[nodiscard]] bool spell(const QByteArray & wordData) const;

        [[nodiscard]] QStringList suggestions(const QString & word) const;
        [[nodiscard]] QStringList suggestions(const QByteArray & wordData) const;

        void add(const QString & word);
        void add(const QByteArray & wordData);

        void remove(const QString & word);
        void remove(const QByteArray & wordData);

    private:
        std::shared_ptr<Hunspell> m_pHunspell;
    };

    class Dictionary
    {
    public:
        [[nodiscard]] bool isEmpty() const noexcept;

    public:
        HunspellWrapper m_hunspellWrapper;
        QString m_dictionaryPath;
        bool m_enabled = true;
    };

private:
    FileIOProcessorAsync * m_pFileIOProcessorAsync;

    Account m_currentAccount;

    std::shared_ptr<QAtomicInt> m_pDictionariesFinderStopFlag;

    // Hashed by the language code
    QHash<QString, Dictionary> m_systemDictionaries;
    bool m_systemDictionariesReady = false;

    QUuid m_readUserDictionaryRequestId;
    QString m_userDictionaryPath;
    QStringList m_userDictionary;
    bool m_userDictionaryReady = false;

    QStringList m_userDictionaryPartPendingWriting;
    QUuid m_appendUserDictionaryPartToFileRequestId;

    QUuid m_updateUserDictionaryFileRequestId;
};

} // namespace quentier
