/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include <quentier/types/Account.h>
#include <quentier/utility/ShortcutManager.h>

#include <QKeySequence>
#include <QObject>

namespace quentier::utility {

class ShortcutManager::ShortcutManagerPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutManagerPrivate(ShortcutManager & shortcutManager);

    [[nodiscard]] QKeySequence shortcut(
        int key, const Account & account, const QString & context) const;

    [[nodiscard]] QKeySequence shortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context) const;

    [[nodiscard]] QKeySequence defaultShortcut(
        int key, const Account & account, const QString & context) const;

    [[nodiscard]] QKeySequence defaultShortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context) const;

    [[nodiscard]] QKeySequence userShortcut(
        int key, const Account & account, const QString & context) const;

    [[nodiscard]] QKeySequence userShortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context) const;

Q_SIGNALS:
    void shortcutChanged(
        int key, QKeySequence shortcut, const Account & account,
        QString context);

    void nonStandardShortcutChanged(
        QString nonStandardKey, QKeySequence shortcut, const Account & account,
        QString context);

public Q_SLOTS:
    void setUserShortcut(
        int key, const QKeySequence & shortcut, const Account & account,
        QString context);

    void setNonStandardUserShortcut(
        QString nonStandardKey, const QKeySequence & shortcut,
        const Account & account, QString context);

    void setDefaultShortcut(
        int key, const QKeySequence & shortcut, const Account & account,
        QString context);

    void setNonStandardDefaultShortcut(
        QString nonStandardKey, const QKeySequence & shortcut,
        const Account & account, QString context);

private:
    [[nodiscard]] QString keyToString(int key) const;

    [[nodiscard]] QString shortcutGroupString(
        const QString & context, bool defaultShortcut,
        bool nonStandardShortcut) const;

private:
    ShortcutManager * const q_ptr;
    Q_DECLARE_PUBLIC(ShortcutManager)
};

} // namespace quentier::utility
