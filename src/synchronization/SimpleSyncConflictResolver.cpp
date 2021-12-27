/*
 * Copyright 2021 Dmitry Ivanov
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

#include "SimpleSyncConflictResolver.h"

#include <utility/Threading.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QTextStream>

namespace quentier::synchronization {

SimpleSyncConflictResolver::SimpleSyncConflictResolver(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleSyncConflictResolver",
            "SimpleSyncConflictResolver ctor: null local storage")}};
    }
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::resolveNotebooksConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine)
{
    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "SimpleSyncConflictResolver::resolveNotebooksConflict: theirs: "
            << theirs << "\nMine: " << mine);

    using Result = ISyncConflictResolver::NotebookConflictResolution;

    if (Q_UNLIKELY(!theirs.guid())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no guid")}});
    }

    if (Q_UNLIKELY(!theirs.name())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no name")}});
    }

    if (Q_UNLIKELY(!mine.guid() && !mine.name())) {
        return utility::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleSyncConflictResolver",
                "Cannot resolve notebook sync conflict: local notebook "
                "has neither name nor guid")}});
    }

    if (mine.name() && (*mine.name() == *theirs.name())) {
        return processNotebooksConflictByName(theirs, std::move(mine));
    }

    return processNotebooksConflictByGuid(theirs, mine);
}

QFuture<ISyncConflictResolver::NoteConflictResolution>
    SimpleSyncConflictResolver::resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::SavedSearchConflictResolution>
    SimpleSyncConflictResolver::resolveSavedSearchConflict(
        qevercloud::SavedSearch theirs, qevercloud::SavedSearch mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::TagConflictResolution>
    SimpleSyncConflictResolver::resolveTagConflict(
        qevercloud::Tag theirs, qevercloud::Tag mine)
{
    // TODO: implement
    return {};
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::processNotebooksConflictByName(
        const qevercloud::Notebook & theirs, qevercloud::Notebook mine)
{
    if (mine.guid() && *mine.guid() == theirs.guid().value())
    {
        QNDEBUG(
            "synchronization::SimpleSyncConflictResolver",
            "Conflicting notebooks match by name and guid => taking the remote "
                << "version");
        return utility::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::UseTheirs{});
    }

    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "Conflicting notebooks match by name but not by guid");

    const auto & mineLinkedNotebookGuid = mine.linkedNotebookGuid();
    const auto & theirsLinkedNotebookGuid = theirs.linkedNotebookGuid();
    if (mineLinkedNotebookGuid != theirsLinkedNotebookGuid) {
        QNDEBUG(
            "synchronization::SimpleSyncConflictResolver",
            "Conflicting notebooks have the same name but their linked "
                << "notebook guids don't match => they are either from "
                << "different linked notebooks or one is from user's own "
                << "account while the other is from some linked notebook");
        return utility::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::IgnoreMine{});
    }

    QNDEBUG(
        "synchronization::SimpleSyncConflictResolver",
        "Both conflicting notebooks are either from user's own account or from "
            << "the same linked notebook");

    QPromise<NotebookConflictResolution> promise;
    auto future = promise.future();

    auto renameNotebookFuture = renameConflictingNotebook(std::move(mine));
    utility::bindPromiseToFuture(
        std::move(promise), std::move(renameNotebookFuture),
        std::function{[](QPromise<NotebookConflictResolution> & promise,
                         QList<qevercloud::Notebook> && notebooks) {
            Q_ASSERT(notebooks.size() == 1);
            promise.addResult(NotebookConflictResolution{
                ConflictResolution::MoveMine<qevercloud::Notebook>{
                    std::move(notebooks[0])}});
        }});

    return future;
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleSyncConflictResolver::processNotebooksConflictByGuid(
        const qevercloud::Notebook & theirs, const qevercloud::Notebook & mine)
{
    // Notebooks conflict by guid, let's understand whether there is a notebook
    // with the same name as theirs in the local storage

    // TODO: implement
    return {};
}

QFuture<qevercloud::Notebook>
    SimpleSyncConflictResolver::renameConflictingNotebook(
        qevercloud::Notebook notebook, int counter)
{
    Q_ASSERT(notebook.name());

    QString newNotebookName;
    QTextStream strm{&newNotebookName};
    strm << notebook.name().value();
    strm << " - ";
    strm << QCoreApplication::translate(
        "synchronization::SimpleSyncConflictResolver", "conflicting");

    if (counter > 1) {
        strm << " (";
        strm << counter;
        strm << ")";
    }

    auto findNotebookFuture = m_localStorage->findNotebookByName(
        newNotebookName, notebook.linkedNotebookGuid());

    if (findNotebookFuture.isFinished()) {
        try {
            findNotebookFuture.waitForFinished();
        }
        catch (const QException & e) {
            // Future contains exception, return it directly to the caller
            return utility::makeExceptionalFuture<qevercloud::Notebook>(e);
        }

        if (findNotebookFuture.resultCount() == 0) {
            // No conflict by name was found in the local storage, can use
            // the suggested notebook name
            notebook.setName(newNotebookName);
            return utility::makeReadyFuture<qevercloud::Notebook>(
                std::move(notebook));
        }

        // Conflict by name was detected, will try once again with another name
        return renameConflictingNotebook(notebook, ++counter);
    }

    QPromise<qevercloud::Notebook> promise;
    auto future = promise.future();

    promise.start();

    auto watcher = utility::makeFutureWatcher<std::optional<qevercloud::Notebook>>();
    watcher->setFuture(findNotebookFuture);
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher,
        &QFutureWatcher<qevercloud::Notebook>::finished,
        rawWatcher,
        [self_weak = weak_from_this(), promise = std::move(promise),
         watcher = std::move(watcher),
         notebook = std::move(notebook),
         newNotebookName = std::move(newNotebookName),
         counter = counter] () mutable
        {
            const auto self = self_weak.lock();
            if (!self) {
                return;
            }

            auto future = watcher->future();
            try {
                future.waitForFinished();
            }
            catch (const QException & e) {
                // Failed to check whether notebook with conflicting name
                // exists in the local storage
                promise.setException(e);
                promise.finish();
                return;
            }

            const auto result = future.result();
            if (!result) {
                // No conflict by name was found in the local storage, can use
                // the suggested notebook name
                notebook.setName(newNotebookName);
                promise.addResult(std::move(notebook));
                promise.finish();
                return;
            }

            // Conflict by name was detected, will try once again with another
            // name
            auto newFuture =
                self->renameConflictingNotebook(notebook, ++counter);

            utility::bindPromiseToFuture(std::move(promise), newFuture);
        });

    return future;
}

} // namespace quentier::synchronization
