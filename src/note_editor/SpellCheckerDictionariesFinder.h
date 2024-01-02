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

#include <QAtomicInt>
#include <QHash>
#include <QObject>
#include <QRunnable>
#include <QSet>
#include <QString>

#include <memory>

namespace quentier {

class SpellCheckerDictionariesFinder final : public QObject, public QRunnable
{
    Q_OBJECT
public:
    using DicAndAffFilesByDictionaryName =
        QHash<QString, std::pair<QString, QString>>;

public:
    SpellCheckerDictionariesFinder(
        std::shared_ptr<QAtomicInt> pStopFlag, QObject * parent = nullptr);

    void run() override;

Q_SIGNALS:
    void foundDictionaries(
        DicAndAffFilesByDictionaryName docAndAffFilesByDictionaryName);

private:
    std::shared_ptr<QAtomicInt> m_pStopFlag;
    DicAndAffFilesByDictionaryName m_files;
    const QSet<QString> m_localeList;
};

} // namespace quentier
