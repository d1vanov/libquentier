/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#define NOTE_EDITOR_ATTACHMENT_SAVE_LOCATIONS_KEY                              \
    QStringLiteral("AttachmentSaveLocations")

#define NOTE_EDITOR_LAST_ATTACHMENT_ADD_LOCATION_KEY                           \
    QStringLiteral("LastAttachmentAddLocation")

#define NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION                   \
    QStringLiteral("Encryption/rememberPassphraseForSession")

#define NOTE_EDITOR_REMOVE_RESOURCE_UNDO_DATA_SIZE_THRESHOLD                   \
    QStringLiteral("RemoveResourceUndoDataSizeThreshold")

// 100 Mb in bytes
#define NOTE_EDITOR_REMOVE_RESOURCE_UNDO_DATA_SIZE_DEFAULT_THRESHOLD (104857600)

#endif // LIB_QUENTIER_NOTE_EDITOR_SETTINGS_NAME_H
