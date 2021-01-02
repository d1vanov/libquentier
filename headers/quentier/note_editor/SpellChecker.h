/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_SPELL_CHECKER_H
#define LIB_QUENTIER_NOTE_EDITOR_SPELL_CHECKER_H

#include <quentier/utility/Linkage.h>

#include <QObject>
#include <QVector>

#include <utility>

namespace quentier {

class Account;
class FileIOProcessorAsync;
class SpellCheckerPrivate;

class QUENTIER_EXPORT SpellChecker : public QObject
{
    Q_OBJECT
public:
    SpellChecker(
        FileIOProcessorAsync * pFileIOProcessorAsync, const Account & account,
        QObject * parent = nullptr, const QString & userDictionaryPath = {});

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

private:
    SpellCheckerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(SpellChecker)
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_SPELL_CHECKER_H
