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

#include "ShortcutManager_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/ShortcutManager.h>

#define SHORTCUT_SETTINGS_NAME QStringLiteral("Shortcuts")

namespace quentier {

ShortcutManagerPrivate::ShortcutManagerPrivate(
    ShortcutManager & shortcutManager) :
    QObject(&shortcutManager),
    q_ptr(&shortcutManager)
{
    Q_Q(ShortcutManager);

    QObject::connect(
        this, &ShortcutManagerPrivate::shortcutChanged, q,
        &ShortcutManager::shortcutChanged);

    QObject::connect(
        this, &ShortcutManagerPrivate::nonStandardShortcutChanged, q,
        &ShortcutManager::nonStandardShortcutChanged);
}

QKeySequence ShortcutManagerPrivate::shortcut(
    const int key, const Account & account, const QString & context) const
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::shortcut: key = "
            << key << ", context = " << context
            << ", account: " << account.name());

    auto userKeySequence = userShortcut(key, account, context);
    if (!userKeySequence.isEmpty()) {
        return userKeySequence;
    }

    QNDEBUG(
        "utility:shortcut",
        "User shortcut is empty, fallback to "
            << "the default shortcut");
    return defaultShortcut(key, account, context);
}

QKeySequence ShortcutManagerPrivate::shortcut(
    const QString & nonStandardKey, const Account & account,
    const QString & context) const
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::shortcut: "
            << "non-standard key = " << nonStandardKey
            << ", context = " << context << ", account: " << account.name());

    auto userKeySequence = userShortcut(nonStandardKey, account, context);

    if (!userKeySequence.isEmpty()) {
        return userKeySequence;
    }

    QNDEBUG(
        "utility:shortcut",
        "User shortcut is empty, fallback to "
            << "the default shortcut");
    return defaultShortcut(nonStandardKey, account, context);
}

QKeySequence ShortcutManagerPrivate::defaultShortcut(
    const int key, const Account & account, const QString & context) const
{
    QString keyString = keyToString(key);

    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::defaultShortcut: "
            << "key = " << keyString << " (" << key
            << "), context = " << context << ", account: " << account.name());

    if (Q_UNLIKELY(keyString.isEmpty())) {
        return QKeySequence();
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);

    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ true,
        /* non-standard shortcut = */ false));

    QVariant value = settings.value(keyString);
    settings.endGroup();

    QKeySequence keySequence;
    if (value.isValid()) {
        keySequence =
            QKeySequence(value.toString(), QKeySequence::PortableText);
    }

    if (keySequence.isEmpty()) {
        QNTRACE(
            "utility:shortcut",
            "Can't find default shortcut in app "
                << "settings");

        if ((key >= 0) && (key < QKeySequence::UnknownKey)) {
            QNTRACE(
                "utility:shortcut",
                "Returning the platform-specific "
                    << "default from QKeySequence");
            return QKeySequence(key);
        }
        else {
            QNTRACE("utility:shortcut", "Returning empty shortcut");
            return QKeySequence();
        }
    }

    QNTRACE("utility:shortcut", "Key sequence: " << keySequence);
    return keySequence;
}

QKeySequence ShortcutManagerPrivate::defaultShortcut(
    const QString & nonStandardKey, const Account & account,
    const QString & context) const
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::defaultShortcut: "
            << "non-standard key = " << nonStandardKey
            << ", context = " << context << ", account: " << account.name());

    if (Q_UNLIKELY(nonStandardKey.isEmpty())) {
        return {};
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);

    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ true,
        /* non-standard shortcut = */ true));

    QVariant value = settings.value(nonStandardKey);
    settings.endGroup();

    QKeySequence keySequence;
    if (value.isValid()) {
        keySequence =
            QKeySequence(value.toString(), QKeySequence::PortableText);
    }

    if (keySequence.isEmpty()) {
        QNTRACE(
            "utility:shortcut",
            "Can't find default shortcut in app "
                << "settings, returning empty shortcut");
        return QKeySequence();
    }

    return keySequence;
}

QKeySequence ShortcutManagerPrivate::userShortcut(
    const int key, const Account & account, const QString & context) const
{
    QString keyString = keyToString(key);

    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::userShortcut: key = "
            << keyString << " (" << key << "), context = " << context
            << ", account: " << account.name());

    if (Q_UNLIKELY(keyString.isEmpty())) {
        return {};
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);

    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ false));

    QVariant value = settings.value(keyString);
    QNTRACE("utility:shortcut", "Read from app settings: " << value);
    settings.endGroup();

    if (!value.isValid()) {
        QNTRACE(
            "utility:shortcut",
            "Couldn't find user shortcut for standard "
                << "key " << keyString << " (" << key << ")");
        return QKeySequence();
    }

    QKeySequence keySequence =
        QKeySequence(value.toString(), QKeySequence::PortableText);

    QNTRACE("utility:shortcut", "Key sequence: " << keySequence);
    return keySequence;
}

QKeySequence ShortcutManagerPrivate::userShortcut(
    const QString & nonStandardKey, const Account & account,
    const QString & context) const
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::userShortcut: "
            << "non-standard key = " << nonStandardKey
            << ", context = " << context << ", account: " << account.name());

    if (Q_UNLIKELY(nonStandardKey.isEmpty())) {
        return QKeySequence();
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);

    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ true));

    QVariant value = settings.value(nonStandardKey);
    QNTRACE("utility:shortcut", "Read from app settings: " << value);
    settings.endGroup();

    if (!value.isValid()) {
        QNTRACE(
            "utility:shortcut",
            "Couldn't find user shortcut for "
                << "non-standard key " << nonStandardKey);
        return {};
    }

    QKeySequence keySequence =
        QKeySequence(value.toString(), QKeySequence::PortableText);

    QNTRACE("utility:shortcut", "Key sequence: " << keySequence);
    return keySequence;
}

void ShortcutManagerPrivate::setUserShortcut(
    int key, QKeySequence shortcut, const Account & account, QString context)
{
    QString keyString = keyToString(key);

    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::setUserShortcut: "
            << "key = " << keyString << " (" << key
            << "), shortcut = " << shortcut << ", context = " << context
            << ", account: " << account.name());

    if (Q_UNLIKELY(keyString.isEmpty())) {
        return;
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ false));

    settings.setValue(keyString, shortcut.toString(QKeySequence::PortableText));
    settings.endGroup();

    QKeySequence actualShortcut = shortcut;
    if (actualShortcut.isEmpty()) {
        actualShortcut = defaultShortcut(key, account, context);
    }

    Q_EMIT shortcutChanged(key, actualShortcut, account, context);
}

void ShortcutManagerPrivate::setNonStandardUserShortcut(
    QString nonStandardKey, QKeySequence shortcut, const Account & account,
    QString context)
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::setNonStandardUserShortcut: non-standard "
            << "key = " << nonStandardKey << ", shortcut = " << shortcut
            << ", context = " << context << ", account: " << account.name());

    if (Q_UNLIKELY(nonStandardKey.isEmpty())) {
        return;
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ true));

    settings.setValue(
        nonStandardKey, shortcut.toString(QKeySequence::PortableText));

    settings.endGroup();

    QKeySequence actualShortcut = shortcut;
    if (actualShortcut.isEmpty()) {
        actualShortcut = defaultShortcut(nonStandardKey, account, context);
    }

    Q_EMIT nonStandardShortcutChanged(
        nonStandardKey, actualShortcut, account, context);
}

void ShortcutManagerPrivate::setDefaultShortcut(
    int key, QKeySequence shortcut, const Account & account, QString context)
{
    QString keyString = keyToString(key);

    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::setDefaultShortcut: "
            << "key = " << keyString << " (" << key
            << "), shortcut = " << shortcut << ", context = " << context
            << ", account: " << account.name());

    if (Q_UNLIKELY(keyString.isEmpty())) {
        return;
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ true,
        /* non-standard shortcut = */ false));

    settings.setValue(keyString, shortcut.toString(QKeySequence::PortableText));
    settings.endGroup();

    // Need to Q_EMIT the notification is there's no user shortcut overriding
    // the default one
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ false));

    QVariant userShortcut = settings.value(keyString);
    settings.endGroup();

    QKeySequence userKeySequence;
    if (userShortcut.isValid()) {
        userKeySequence =
            QKeySequence(userShortcut.toString(), QKeySequence::PortableText);
    }

    if (userKeySequence.isEmpty()) {
        QNTRACE(
            "utility:shortcut",
            "Found no user shortcut overriding "
                << "the default one");
        Q_EMIT shortcutChanged(key, shortcut, account, context);
    }
}

void ShortcutManagerPrivate::setNonStandardDefaultShortcut(
    QString nonStandardKey, QKeySequence shortcut, const Account & account,
    QString context)
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::setNonStandardDefaultShortcut: "
            << "non-standard key = " << nonStandardKey
            << ", shortcut = " << shortcut << ", context = " << context
            << ", account: " << account.name());

    if (Q_UNLIKELY(nonStandardKey.isEmpty())) {
        return;
    }

    ApplicationSettings settings(account, SHORTCUT_SETTINGS_NAME);
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ true,
        /* non-standard shortcut = */ true));

    settings.setValue(
        nonStandardKey, shortcut.toString(QKeySequence::PortableText));

    settings.endGroup();

    // Need to Q_EMIT the notification is there's no user shortcut overriding
    // the default one
    settings.beginGroup(shortcutGroupString(
        context,
        /* default shortcut = */ false,
        /* non-standard shortcut = */ true));

    QVariant userShortcut = settings.value(nonStandardKey);
    settings.endGroup();

    QKeySequence userKeySequence;
    if (userShortcut.isValid()) {
        userKeySequence =
            QKeySequence(userShortcut.toString(), QKeySequence::PortableText);
    }

    if (userKeySequence.isEmpty()) {
        QNTRACE(
            "utility:shortcut",
            "Found no user shortcut overriding "
                << "the default one");

        Q_EMIT nonStandardShortcutChanged(
            nonStandardKey, shortcut, account, context);
    }
}

QString ShortcutManagerPrivate::keyToString(const int key) const
{
    QNTRACE(
        "utility:shortcut",
        "ShortcutManagerPrivate::keyToString: key = " << key);

    if (key < ShortcutManager::NewNote) {
#define PRINT_ITEM(item)                                                       \
    case QKeySequence::item:                                                   \
        return QStringLiteral(#item)

        switch (key) {
            PRINT_ITEM(AddTab);
            PRINT_ITEM(Back);
            PRINT_ITEM(Bold);
            PRINT_ITEM(Close);
            PRINT_ITEM(Copy);
            PRINT_ITEM(Cut);
            PRINT_ITEM(DeleteEndOfLine);
            PRINT_ITEM(DeleteEndOfWord);
            PRINT_ITEM(DeleteStartOfWord);
            PRINT_ITEM(Find);
            PRINT_ITEM(FindNext);
            PRINT_ITEM(FindPrevious);
            PRINT_ITEM(Forward);
            PRINT_ITEM(HelpContents);
            PRINT_ITEM(InsertLineSeparator);
            PRINT_ITEM(InsertParagraphSeparator);
            PRINT_ITEM(Italic);
            PRINT_ITEM(MoveToEndOfBlock);
            PRINT_ITEM(MoveToEndOfDocument);
            PRINT_ITEM(MoveToEndOfLine);
            PRINT_ITEM(MoveToNextChar);
            PRINT_ITEM(MoveToNextLine);
            PRINT_ITEM(MoveToNextPage);
            PRINT_ITEM(MoveToNextWord);
            PRINT_ITEM(MoveToPreviousChar);
            PRINT_ITEM(MoveToPreviousLine);
            PRINT_ITEM(MoveToPreviousPage);
            PRINT_ITEM(MoveToPreviousWord);
            PRINT_ITEM(MoveToStartOfBlock);
            PRINT_ITEM(MoveToStartOfDocument);
            PRINT_ITEM(MoveToStartOfLine);
            PRINT_ITEM(New);
            PRINT_ITEM(NextChild);
            PRINT_ITEM(Open);
            PRINT_ITEM(Paste);
            PRINT_ITEM(Preferences);
            PRINT_ITEM(PreviousChild);
            PRINT_ITEM(Print);
            PRINT_ITEM(Quit);
            PRINT_ITEM(Redo);
            PRINT_ITEM(Refresh);
            PRINT_ITEM(Replace);
            PRINT_ITEM(SaveAs);
            PRINT_ITEM(Save);
            PRINT_ITEM(SelectAll);
            PRINT_ITEM(SelectEndOfBlock);
            PRINT_ITEM(SelectEndOfDocument);
            PRINT_ITEM(SelectEndOfLine);
            PRINT_ITEM(SelectNextChar);
            PRINT_ITEM(SelectNextLine);
            PRINT_ITEM(SelectNextPage);
            PRINT_ITEM(SelectNextWord);
            PRINT_ITEM(SelectPreviousChar);
            PRINT_ITEM(SelectPreviousLine);
            PRINT_ITEM(SelectPreviousPage);
            PRINT_ITEM(SelectPreviousWord);
            PRINT_ITEM(SelectStartOfBlock);
            PRINT_ITEM(SelectStartOfDocument);
            PRINT_ITEM(SelectStartOfLine);
            PRINT_ITEM(Underline);
            PRINT_ITEM(Undo);
            PRINT_ITEM(UnknownKey);
            PRINT_ITEM(WhatsThis);
            PRINT_ITEM(ZoomIn);
            PRINT_ITEM(ZoomOut);
            PRINT_ITEM(FullScreen);
            PRINT_ITEM(DeleteCompleteLine);
            PRINT_ITEM(Delete);
            PRINT_ITEM(Deselect);
            PRINT_ITEM(Backspace);
        default:
        {
            QNDEBUG(
                "utility:shortcut",
                "The key " << key << " doesn't correspond to any of "
                           << "QKeySequence::StandardKey items");
            return {};
        }
        }
    }

#undef PRINT_ITEM

    switch (key) {
#define PRINT_ITEM(item)                                                       \
    case ShortcutManager::item:                                                \
        return QStringLiteral(#item)

        PRINT_ITEM(NewNote);
        PRINT_ITEM(NewTag);
        PRINT_ITEM(NewNotebook);
        PRINT_ITEM(NewSavedSearch);
        PRINT_ITEM(AddAttachment);
        PRINT_ITEM(SaveAttachment);
        PRINT_ITEM(OpenAttachment);
        PRINT_ITEM(CopyAttachment);
        PRINT_ITEM(CutAttachment);
        PRINT_ITEM(RemoveAttachment);
        PRINT_ITEM(AddAccount);
        PRINT_ITEM(ExitAccount);
        PRINT_ITEM(SwitchAccount);
        PRINT_ITEM(AccountInfo);
        PRINT_ITEM(NoteSearch);
        PRINT_ITEM(NewNoteSearch);
        PRINT_ITEM(ShowNotes);
        PRINT_ITEM(ShowNotebooks);
        PRINT_ITEM(ShowTags);
        PRINT_ITEM(ShowSavedSearches);
        PRINT_ITEM(ShowDeletedNotes);
        PRINT_ITEM(ShowStatusBar);
        PRINT_ITEM(ShowToolBar);
        PRINT_ITEM(PasteUnformatted);
        PRINT_ITEM(Font);
        PRINT_ITEM(UpperIndex);
        PRINT_ITEM(LowerIndex);
        PRINT_ITEM(AlignLeft);
        PRINT_ITEM(AlignCenter);
        PRINT_ITEM(AlignRight);
        PRINT_ITEM(IncreaseIndentation);
        PRINT_ITEM(DecreaseIndentation);
        PRINT_ITEM(IncreaseFontSize);
        PRINT_ITEM(DecreaseFontSize);
        PRINT_ITEM(InsertNumberedList);
        PRINT_ITEM(InsertBulletedList);
        PRINT_ITEM(Strikethrough);
        PRINT_ITEM(Highlight);
        PRINT_ITEM(InsertTable);
        PRINT_ITEM(InsertRow);
        PRINT_ITEM(InsertColumn);
        PRINT_ITEM(RemoveRow);
        PRINT_ITEM(RemoveColumn);
        PRINT_ITEM(InsertHorizontalLine);
        PRINT_ITEM(InsertToDoTag);
        PRINT_ITEM(EditHyperlink);
        PRINT_ITEM(CopyHyperlink);
        PRINT_ITEM(RemoveHyperlink);
        PRINT_ITEM(Encrypt);
        PRINT_ITEM(Decrypt);
        PRINT_ITEM(DecryptPermanently);
        PRINT_ITEM(BackupLocalStorage);
        PRINT_ITEM(RestoreLocalStorage);
        PRINT_ITEM(UpgradeLocalStorage);
        PRINT_ITEM(LocalStorageStatus);
        PRINT_ITEM(SpellCheck);
        PRINT_ITEM(SaveImage);
        PRINT_ITEM(AnnotateImage);
        PRINT_ITEM(ImageRotateClockwise);
        PRINT_ITEM(ImageRotateCounterClockwise);
        PRINT_ITEM(Synchronize);
        PRINT_ITEM(FullSync);
        PRINT_ITEM(ImportFolders);
        PRINT_ITEM(Preferences);
        PRINT_ITEM(ReleaseNotes);
        PRINT_ITEM(ViewLogs);
        PRINT_ITEM(About);
        PRINT_ITEM(UnknownKey);
    default:
    {
        QNDEBUG(
            "utility:shortcut",
            "The key " << key << " doesn't correspond to any of "
                       << "ShortcutManager::QuentierShortcutKey items");
        return {};
    }
    }

#undef PRINT_ITEM
}

QString ShortcutManagerPrivate::shortcutGroupString(
    const QString & context, const bool defaultShortcut,
    const bool nonStandardShortcut) const
{
    QNDEBUG(
        "utility:shortcut",
        "ShortcutManagerPrivate::shortcutGroupString: "
            << "context = " << context
            << ", default shortcut = " << (defaultShortcut ? "true" : "false")
            << ", non-standard shortcut = "
            << (nonStandardShortcut ? "true" : "false"));

    return (defaultShortcut ? QStringLiteral("DefaultShortcuts-")
                            : QStringLiteral("UserShortcuts-")) +
        (context.isEmpty()
             ? (nonStandardShortcut ? QStringLiteral("NonStandard")
                                    : QStringLiteral("General"))
             : context);
}

} // namespace quentier
