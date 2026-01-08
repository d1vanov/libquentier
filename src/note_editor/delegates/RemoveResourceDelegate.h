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

#include "JsResultCallbackFunctor.hpp"

#include <quentier/local_storage/Fwd.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>

#include <QObject>

namespace quentier {

class NoteEditorPrivate;

class RemoveResourceDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit RemoveResourceDelegate(
        qevercloud::Resource resourceToRemove, NoteEditorPrivate & noteEditor,
        local_storage::ILocalStoragePtr localStorage);

    void start();

Q_SIGNALS:
    void finished(qevercloud::Resource removedResource, bool reversible);
    void cancelled(QString resourceLocalId);
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);
    void onResourceReferenceRemovedFromNoteContent(const QVariant & data);

private:
    void doStart();
    void removeResourceFromNoteEditorPage();

private:
    using JsCallback = JsResultCallbackFunctor<RemoveResourceDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    const local_storage::ILocalStoragePtr m_localStorage;

    qevercloud::Resource m_resource;
    bool m_reversible = true;
};

} // namespace quentier
