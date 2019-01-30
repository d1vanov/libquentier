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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PRIVATE_MACROS_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PRIVATE_MACROS_H

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(this->page()); \
    if (Q_UNLIKELY(!page)) { \
        QNERROR(QStringLiteral("Can't get access to note editor's underlying page!")); \
        return; \
    }

#define CHECK_NOTE_EDITABLE(message) \
    if (Q_UNLIKELY(!isPageEditable())) { \
        ErrorString error(message); \
        error.appendBase(QT_TRANSLATE_NOOP("NoteEditorPrivate", \
                                           "Note is not editable")); \
        QNINFO(error << QStringLiteral(", note: ") \
               << (m_pNote.isNull() \
                   ? QStringLiteral("<null>") \
                   : m_pNote->toString()) \
               << QStringLiteral("\nNotebook: ") \
               << (m_pNotebook.isNull() \
                   ? QStringLiteral("<null>") \
                   : m_pNotebook->toString())); \
        Q_EMIT notifyError(error); \
        return; \
    }

#define CHECK_ACCOUNT(message, ...) \
    if (Q_UNLIKELY(m_pAccount.isNull())) { \
        ErrorString error(message); \
        error.appendBase(QT_TRANSLATE_NOOP("NoteEditorPrivate", \
                                           "No account is associated with "\
                                           "the note editor")); \
        QNWARNING(error); \
        Q_EMIT notifyError(error); \
        return __VA_ARGS__; \
    }

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PRIVATE_MACROS_H
