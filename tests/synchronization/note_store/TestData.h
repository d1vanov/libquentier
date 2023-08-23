/*
 * Copyright 2023 Dmitry Ivanov
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

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QList>

namespace quentier::synchronization::tests::note_store {

struct TestData
{
    // Data from user's own account
    QList<qevercloud::Notebook> m_userOwnBaseNotebooks;
    QList<qevercloud::Notebook> m_userOwnModifiedNotebooks;
    QList<qevercloud::Notebook> m_userOwnNewNotebooks;

    QList<qevercloud::Tag> m_userOwnBaseTags;
    QList<qevercloud::Tag> m_userOwnModifiedTags;
    QList<qevercloud::Tag> m_userOwnNewTags;

    QList<qevercloud::SavedSearch> m_baseSavedSearches;
    QList<qevercloud::SavedSearch> m_modifiedSavedSearches;
    QList<qevercloud::SavedSearch> m_newSavedSearches;

    QList<qevercloud::Note> m_userOwnBaseNotes;
    QList<qevercloud::Note> m_userOwnModifiedNotes;
    QList<qevercloud::Note> m_userOwnNewNotes;

    QList<qevercloud::Resource> m_userOwnModifiedResources;

    QList<qevercloud::LinkedNotebook> m_baseLinkedNotebooks;
    QList<qevercloud::LinkedNotebook> m_modifiedLinkedNotebooks;
    QList<qevercloud::LinkedNotebook> m_newLinkedNotebooks;

    QSet<qevercloud::Guid> m_expungedUserOwnNotebookGuids;
    QSet<qevercloud::Guid> m_expungedUserOwnTagGuids;
    QSet<qevercloud::Guid> m_expungedUserOwnSavedSearchGuids;
    QSet<qevercloud::Guid> m_expungedUserOwnNoteGuids;

    // Data from linked notebooks
    QList<qevercloud::Notebook> m_linkedNotebookBaseNotebooks;
    QList<qevercloud::Notebook> m_linkedNotebookModifiedNotebooks;
    QList<qevercloud::Notebook> m_linkedNotebookNewNotebooks;

    QList<qevercloud::Tag> m_linkedNotebookBaseTags;
    QList<qevercloud::Tag> m_linkedNotebookModifiedTags;
    QList<qevercloud::Tag> m_linkedNotebookNewTags;

    QList<qevercloud::Note> m_linkedNotebookBaseNotes;
    QList<qevercloud::Note> m_linkedNotebookModifiedNotes;
    QList<qevercloud::Note> m_linkedNotebookNewNotes;

    QList<qevercloud::Resource> m_linkedNotebookModifiedResources;

    QHash<qevercloud::Guid, QSet<qevercloud::Guid>>
        m_expungedLinkedNotebookNotebookGuids;

    QHash<qevercloud::Guid, QSet<qevercloud::Guid>>
        m_expungedLinkedNotebookTagGuids;

    QHash<qevercloud::Guid, QSet<qevercloud::Guid>>
        m_expungedLinkedNotebookNoteGuids;
};

} // namespace quentier::synchronization::tests::note_store
