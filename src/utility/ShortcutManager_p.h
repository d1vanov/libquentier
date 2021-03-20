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

#ifndef LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_PRIVATE_H
#define LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_PRIVATE_H

#include <quentier/types/Account.h>

#include <QKeySequence>
#include <QObject>

namespace quentier {

class ShortcutManager;

class Q_DECL_HIDDEN ShortcutManagerPrivate final : public QObject
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
        int key, QKeySequence shortcut, const Account & account,
        QString context);

    void setNonStandardUserShortcut(
        QString nonStandardKey, QKeySequence shortcut, const Account & account,
        QString context);

    void setDefaultShortcut(
        int key, QKeySequence shortcut, const Account & account,
        QString context);

    void setNonStandardDefaultShortcut(
        QString nonStandardKey, QKeySequence shortcut, const Account & account,
        QString context);

private:
    [[nodiscard]] QString keyToString(int key) const;

    [[nodiscard]] QString shortcutGroupString(
        const QString & context, bool defaultShortcut,
        bool nonStandardShortcut) const;

private:
    ShortcutManager * const q_ptr;
    Q_DECLARE_PUBLIC(ShortcutManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_PRIVATE_H
