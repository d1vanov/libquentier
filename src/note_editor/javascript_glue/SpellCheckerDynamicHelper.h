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

#include <QObject>
#include <QStringList>
#include <QVariant>

namespace quentier {

class SpellCheckerDynamicHelper final : public QObject
{
    Q_OBJECT
public:
    explicit SpellCheckerDynamicHelper(QObject * parent = nullptr);

Q_SIGNALS:
    void lastEnteredWords(QStringList words);

public Q_SLOTS:
    // NOTE: working around https://bugreports.qt.io/browse/QTBUG-39951 -
    // JavaScript array doesn't get automatically converted to QVariant
    void setLastEnteredWords(QVariant words);
};

} // namespace quentier
