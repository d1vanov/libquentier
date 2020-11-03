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

#ifndef LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_H
#define LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_H

#include <quentier/types/Account.h>
#include <quentier/utility/Linkage.h>

#include <QKeySequence>
#include <QObject>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ShortcutManagerPrivate)

class QUENTIER_EXPORT ShortcutManager : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutManager(QObject * parent = nullptr);

    enum QuentierShortcutKey
    {
        NewNote = 5000,
        NewTag,
        NewNotebook,
        NewSavedSearch,
        AddAttachment,
        SaveAttachment,
        OpenAttachment,
        CopyAttachment,
        CutAttachment,
        RemoveAttachment,
        RenameAttachment,
        AddAccount,
        ExitAccount,
        SwitchAccount,
        AccountInfo,
        NoteSearch,
        NewNoteSearch,
        ShowNotes,
        ShowNotebooks,
        ShowTags,
        ShowSavedSearches,
        ShowDeletedNotes,
        ShowStatusBar,
        ShowToolBar,
        PasteUnformatted,
        Font,
        UpperIndex,
        LowerIndex,
        AlignLeft,
        AlignCenter,
        AlignRight,
        AlignFull,
        IncreaseIndentation,
        DecreaseIndentation,
        IncreaseFontSize,
        DecreaseFontSize,
        InsertNumberedList,
        InsertBulletedList,
        Strikethrough,
        Highlight,
        InsertTable,
        InsertRow,
        InsertColumn,
        RemoveRow,
        RemoveColumn,
        InsertHorizontalLine,
        InsertToDoTag,
        EditHyperlink,
        CopyHyperlink,
        RemoveHyperlink,
        Encrypt,
        Decrypt,
        DecryptPermanently,
        BackupLocalStorage,
        RestoreLocalStorage,
        UpgradeLocalStorage,
        LocalStorageStatus,
        SpellCheck,
        SpellCheckIgnoreWord,
        SpellCheckAddWordToUserDictionary,
        SaveImage,
        AnnotateImage,
        ImageRotateClockwise,
        ImageRotateCounterClockwise,
        Synchronize,
        FullSync,
        ImportFolders,
        Preferences,
        ReleaseNotes,
        ViewLogs,
        About,
        UnknownKey = 100000
    };

    /**
     * @return              Active shortcut for the standard key - either
     *                      the user defined shortcut (if present) or
     *                      the default one (if present as well)
     */
    QKeySequence shortcut(
        const int key, const Account & account,
        const QString & context = {}) const;

    /**
     * @return              Active shortcut for the non-standard key - either
     *                      the user defined shortcut (if present) or
     *                      the default one (if present as well)
     */
    QKeySequence shortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context = {}) const;

    /**
     * @return              Default shortcut for the standard key if present,
     *                      otherwise empty key sequence
     */
    QKeySequence defaultShortcut(
        const int key, const Account & account,
        const QString & context = {}) const;

    /**
     * @return              Default shortcut for the non-standard key if
     *                      present, otherwise empty key sequence
     */
    QKeySequence defaultShortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context = {}) const;

    /**
     * @return              User defined shortcut for the standard key if
     *                      present, otherwise empty key sequence
     */
    QKeySequence userShortcut(
        const int key, const Account & account,
        const QString & context = {}) const;

    /**
     * @return              User defined shortcut for the non-standard key if
     *                      present, otherwise empty key sequence
     */
    QKeySequence userShortcut(
        const QString & nonStandardKey, const Account & account,
        const QString & context = {}) const;

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
        QString context = {});

    void setNonStandardUserShortcut(
        QString nonStandardKey, QKeySequence shortcut, const Account & account,
        QString context = {});

    void setDefaultShortcut(
        int key, QKeySequence shortcut, const Account & account,
        QString context = {});

    void setNonStandardDefaultShortcut(
        QString nonStandardKey, QKeySequence shortcut, const Account & account,
        QString context = {});

private:
    ShortcutManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(ShortcutManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_SHORTCUT_MANAGER_H
