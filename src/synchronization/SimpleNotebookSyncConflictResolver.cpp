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

#include "SimpleNotebookSyncConflictResolver.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <QCoreApplication>
#include <QTextStream>

namespace quentier::synchronization {

SimpleNotebookSyncConflictResolver::SimpleNotebookSyncConflictResolver(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SimpleNotebookSyncConflictResolver",
            "SimpleNotebookSyncConflictResolver ctor: local storage is null")}};
    }
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleNotebookSyncConflictResolver::resolveNotebooksConflict(
        qevercloud::Notebook theirs, qevercloud::Notebook mine)
{
    QNDEBUG(
        "synchronization::SimpleNotebookSyncConflictResolver",
        "SimpleNotebookSyncConflictResolver::resolveNotebooksConflict: theirs: "
            << theirs << "\nMine: " << mine);

    using Result = ISyncConflictResolver::NotebookConflictResolution;

    if (Q_UNLIKELY(!theirs.guid())) {
        return threading::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleNotebookSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no guid")}});
    }

    if (Q_UNLIKELY(!theirs.name())) {
        return threading::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleNotebookSyncConflictResolver",
                "Cannot resolve notebook sync conflict: remote notebook "
                "has no name")}});
    }

    if (Q_UNLIKELY(!mine.guid() && !mine.name())) {
        return threading::makeExceptionalFuture<Result>(
            InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SimpleNotebookSyncConflictResolver",
                "Cannot resolve notebook sync conflict: local notebook "
                "has neither name nor guid")}});
    }

    if (mine.name() && (*mine.name() == *theirs.name())) {
        return processNotebooksConflictByName(theirs, std::move(mine));
    }

    return processNotebooksConflictByGuid(std::move(theirs));
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleNotebookSyncConflictResolver::processNotebooksConflictByName(
        const qevercloud::Notebook & theirs, qevercloud::Notebook mine)
{
    if (mine.guid() && *mine.guid() == theirs.guid().value()) {
        QNDEBUG(
            "synchronization::SimpleNotebookSyncConflictResolver",
            "Conflicting notebooks match by name and guid => taking the remote "
                << "version");
        return threading::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::UseTheirs{});
    }

    QNDEBUG(
        "synchronization::SimpleNotebookSyncConflictResolver",
        "Conflicting notebooks match by name but not by guid");

    const auto & mineLinkedNotebookGuid = mine.linkedNotebookGuid();
    const auto & theirsLinkedNotebookGuid = theirs.linkedNotebookGuid();
    if (mineLinkedNotebookGuid != theirsLinkedNotebookGuid) {
        QNDEBUG(
            "synchronization::SimpleNotebookSyncConflictResolver",
            "Conflicting notebooks have the same name but their linked "
                << "notebook guids don't match => they are either from "
                << "different linked notebooks or one is from user's own "
                << "account while the other is from some linked notebook");
        return threading::makeReadyFuture<NotebookConflictResolution>(
            ConflictResolution::IgnoreMine{});
    }

    QNDEBUG(
        "synchronization::SimpleNotebookSyncConflictResolver",
        "Both conflicting notebooks are either from user's own account or from "
            << "the same linked notebook");

    auto renameNotebookFuture = renameConflictingNotebook(std::move(mine));
    return threading::then(
        std::move(renameNotebookFuture), [](qevercloud::Notebook notebook) {
            return NotebookConflictResolution{
                ConflictResolution::MoveMine<qevercloud::Notebook>{
                    std::move(notebook)}};
        });
}

QFuture<ISyncConflictResolver::NotebookConflictResolution>
    SimpleNotebookSyncConflictResolver::processNotebooksConflictByGuid(
        qevercloud::Notebook theirs)
{
    // Notebooks conflict by guid, let's understand whether there is a notebook
    // with the same name as theirs in the local storage
    Q_ASSERT(theirs.name());
    auto findNotebookFuture =
        m_localStorage->findNotebookByName(theirs.name().value());

    auto promise = std::make_shared<QPromise<NotebookConflictResolution>>();
    auto future = promise->future();

    promise->start();

    threading::then(
        std::move(findNotebookFuture),
        [theirs = std::move(theirs), promise = std::move(promise),
         self_weak = weak_from_this()](
            std::optional<qevercloud::Notebook> notebook) mutable {
            if (!notebook) {
                // There is no conflict by name in the local storage
                promise->addResult(NotebookConflictResolution{
                    ConflictResolution::UseTheirs{}});
                return;
            }

            auto self = self_weak.lock();
            if (!self) {
                // The resolver is dead, the result doesn't matter
                promise->addResult(NotebookConflictResolution{
                    ConflictResolution::UseTheirs{}});
                return;
            }

            // There is a notebook in the local storage which conflicts by name
            // with theirs notebook
            auto future = self->processNotebooksConflictByName(
                theirs, std::move(*notebook));

            auto thenFuture = threading::then(
                std::move(future),
                [promise](NotebookConflictResolution resolution) {
                    promise->addResult(resolution);
                    promise->finish();
                });

            threading::onFailed(
                std::move(thenFuture), [promise](const QException & e) {
                    promise->setException(e);
                    promise->finish();
                });
        });

    return future;
}

QFuture<qevercloud::Notebook>
    SimpleNotebookSyncConflictResolver::renameConflictingNotebook(
        qevercloud::Notebook notebook, int counter)
{
    Q_ASSERT(notebook.name());

    QString newNotebookName;
    QTextStream strm{&newNotebookName};
    strm << notebook.name().value();
    strm << " - ";
    strm << QCoreApplication::translate(
        "synchronization::SimpleNotebookSyncConflictResolver", "conflicting");

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
            return threading::makeExceptionalFuture<qevercloud::Notebook>(e);
        }

        if (findNotebookFuture.resultCount() == 0) {
            // No conflict by name was found in the local storage, can use
            // the suggested notebook name
            notebook.setName(newNotebookName);
            return threading::makeReadyFuture<qevercloud::Notebook>(
                std::move(notebook));
        }

        // Conflict by name was detected, will try once again with another name
        return renameConflictingNotebook(notebook, ++counter);
    }

    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();

    promise->start();

    auto watcher =
        threading::makeFutureWatcher<std::optional<qevercloud::Notebook>>();
    watcher->setFuture(findNotebookFuture);
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<qevercloud::Notebook>::finished, rawWatcher,
        [self_weak = weak_from_this(), promise = std::move(promise),
         watcher = std::move(watcher), notebook = std::move(notebook),
         newNotebookName = std::move(newNotebookName),
         counter = counter]() mutable {
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
                promise->setException(e);
                promise->finish();
                return;
            }

            const auto result = future.result();
            if (!result) {
                // No conflict by name was found in the local storage, can use
                // the suggested notebook name
                notebook.setName(newNotebookName);
                promise->addResult(std::move(notebook));
                promise->finish();
                return;
            }

            // Conflict by name was detected, will try once again with another
            // name
            auto newFuture =
                self->renameConflictingNotebook(notebook, ++counter);

            QFuture<void> thenFuture = threading::then(
                std::move(newFuture),
                [promise](qevercloud::Notebook notebook) mutable {
                    promise->addResult(std::move(notebook));
                    promise->finish();
                });

            threading::onFailed(
                std::move(thenFuture), [promise](const QException & e) mutable {
                    promise->setException(e);
                    promise->finish();
                });
        });

    return future;
}

} // namespace quentier::synchronization
