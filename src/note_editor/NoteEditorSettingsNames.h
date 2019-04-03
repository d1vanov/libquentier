/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_SETTINGS_NAME_H
#define LIB_QUENTIER_NOTE_EDITOR_SETTINGS_NAME_H

#define NOTE_EDITOR_SETTINGS_NAME QStringLiteral("NoteEditor")

#define NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY \
    QStringLiteral("AttachmentSaveLocations")

#define NOTE_EDITOR_LAST_ATTACHMENT_ADD_LOCATION_KEY \
    QStringLiteral("LastAttachmentAddLocation")

#define NOTE_EDITOR_REMEMBER_PASSPHRASE_FOR_SESSION_KEY \
    QStringLiteral("Encryption/rememberPassphraseForSession")

#define NOTE_EDITOR_DEFAULT_COLORS_GROUP_KEY \
    QStringLiteral("DefaultColors")

#define NOTE_EDITOR_DEFAULT_FONT_COLOR_KEY \
    QStringLiteral("DefaultFontColor")

#define NOTE_EDITOR_DEFAULT_BACKGROUND_COLOR_KEY \
    QStringLiteral("DefaultBackgroundColor")

#define NOTE_EDITOR_DEFAULT_HIGHLIGHT_COLOR_KEY \
    QStringLiteral("DefaultHighlightColor")

#define NOTE_EDITOR_DEFAULT_HIGHLIGHT_TEXT_COLOR_KEY \
    QStringLiteral("DefaultHighlightTextColor")

#endif // LIB_QUENTIER_NOTE_EDITOR_SETTINGS_NAME_H
