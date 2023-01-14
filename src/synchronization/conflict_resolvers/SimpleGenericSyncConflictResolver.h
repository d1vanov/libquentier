/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/Fwd.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Tag.h>

#include <QCoreApplication>
#include <QTextStream>

#include <functional>
#include <memory>
#include <type_traits>
#include <typeinfo>

namespace quentier::synchronization {

// declaration

template <class T, class Resolution, class FindByNameMemFn>
class SimpleGenericSyncConflictResolver :
    public std::enable_shared_from_this<
        SimpleGenericSyncConflictResolver<T, Resolution, FindByNameMemFn>>
{
public:
    SimpleGenericSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage,
        FindByNameMemFn findByNameMemFn, QString typeName);

    [[nodiscard]] QFuture<Resolution> resolveConflict(T theirs, T mine);

private:
    [[nodiscard]] QFuture<Resolution> processConflictByName(
        const T & theirs, T mine);

    [[nodiscard]] QFuture<Resolution> processConflictByGuid(T theirs);

    [[nodiscard]] QFuture<T> renameConflictingItem(T item, int counter = 1);

private:
    local_storage::ILocalStoragePtr m_localStorage;
    FindByNameMemFn m_findByNameMemFn;
    QString m_typeName;
};

// implementation

namespace detail {

template <class T>
struct hasLinkedNoteookGuid : std::false_type
{};

template <>
struct hasLinkedNoteookGuid<qevercloud::Notebook> : std::true_type
{};

template <>
struct hasLinkedNoteookGuid<qevercloud::Tag> : std::true_type
{};

} // namespace detail

template <class T, class Resolution, class FindByNameMemFn>
SimpleGenericSyncConflictResolver<T, Resolution, FindByNameMemFn>::
    SimpleGenericSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage,
        FindByNameMemFn findByNameMemFn, QString typeName) :
    m_localStorage{std::move(localStorage)},
    m_findByNameMemFn{std::move(findByNameMemFn)}, m_typeName{
                                                       std::move(typeName)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{QStringLiteral(
            "SimpleGenericSyncConflictResolver ctor: local storage is null")};
        error.details() = m_typeName;
        throw InvalidArgument{std::move(error)};
    }

    if (Q_UNLIKELY(!m_findByNameMemFn)) {
        ErrorString error{QStringLiteral(
            "SimpleGenericSyncConflictResolver ctor: find by name mem fn is "
            "null")};
        error.details() = m_typeName;
        throw InvalidArgument{std::move(error)};
    }

    if (Q_UNLIKELY(m_typeName.isEmpty())) {
        ErrorString error{QStringLiteral(
            "SimpleGenericSyncConflictResolver ctor: type name is null")};
        error.details() = QString::fromStdString(std::string{typeid(T).name()});
        throw InvalidArgument{std::move(error)};
    }
}

template <class T, class Resolution, class FindByNameMemFn>
QFuture<Resolution> SimpleGenericSyncConflictResolver<
    T, Resolution, FindByNameMemFn>::resolveConflict(T theirs, T mine)
{
    QNDEBUG(
        "synchronization::SimpleGenericSyncConflictResolver",
        "SimpleGenericSyncConflictResolver<"
            << m_typeName << ">::resolveConflict: theirs: " << theirs
            << "\nMine: " << mine);

    if (Q_UNLIKELY(!theirs.guid())) {
        ErrorString error{QStringLiteral(
            "Cannot resolve sync conflict: remote item has no guid")};
        error.details() = m_typeName;
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{std::move(error)});
    }

    if (Q_UNLIKELY(!theirs.name())) {
        ErrorString error{QStringLiteral(
            "Cannot resolve sync conflict: remote item has no name")};
        error.details() = m_typeName;
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{std::move(error)});
    }

    if (Q_UNLIKELY(!mine.guid() && !mine.name())) {
        ErrorString error{QStringLiteral(
            "Cannot resolve notebook sync conflict: local notebook "
            "has neither name nor guid")};
        error.details() = m_typeName;
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{std::move(error)});
    }

    if (mine.name() && (*mine.name() == *theirs.name())) {
        return processConflictByName(theirs, std::move(mine));
    }

    if (mine.guid() && (*mine.guid() == *theirs.guid())) {
        return processConflictByGuid(std::move(theirs));
    }

    return threading::makeReadyFuture<Resolution>(
        ISyncConflictResolver::ConflictResolution::IgnoreMine{});
}

template <class T, class Resolution, class FindByNameMemFn>
QFuture<Resolution> SimpleGenericSyncConflictResolver<
    T, Resolution,
    FindByNameMemFn>::processConflictByName(const T & theirs, T mine)
{
    if (mine.guid() && *mine.guid() == theirs.guid().value()) {
        QNDEBUG(
            "synchronization::SimpleGenericSyncConflictResolver",
            "Conflicting " << m_typeName
                           << " items match by name and guid => "
                              "taking the remote version");
        return threading::makeReadyFuture<Resolution>(
            ISyncConflictResolver::ConflictResolution::UseTheirs{});
    }

    QNDEBUG(
        "synchronization::SimpleGenericSyncConflictResolver",
        "Conflicting " << m_typeName << "items match by name but not by guid");

    if constexpr (detail::hasLinkedNoteookGuid<T>::value) {
        const auto & mineLinkedNotebookGuid = mine.linkedNotebookGuid();
        const auto & theirsLinkedNotebookGuid = theirs.linkedNotebookGuid();
        if (mineLinkedNotebookGuid != theirsLinkedNotebookGuid) {
            QNDEBUG(
                "synchronization::SimpleGenericSyncConflictResolver",
                "Conflicting "
                    << m_typeName << " items have the same name but "
                    << "their linked notebook guids don't match => they are "
                    << "either from different linked notebooks or one is from "
                    << "user's own account while the other is from some linked "
                    << "notebook");
            return threading::makeReadyFuture<Resolution>(
                ISyncConflictResolver::ConflictResolution::IgnoreMine{});
        }

        QNDEBUG(
            "synchronization::SimpleGenericSyncConflictResolver",
            "Both conflicting "
                << m_typeName << " items are either from "
                << "user's own account or from the same linked notebook");
    }

    auto renameItemFuture = renameConflictingItem(std::move(mine));
    return threading::then(std::move(renameItemFuture), [](T item) {
        return Resolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<T>{
                std::move(item)}};
    });
}

template <class T, class Resolution, class FindByNameMemFn>
QFuture<Resolution> SimpleGenericSyncConflictResolver<
    T, Resolution, FindByNameMemFn>::processConflictByGuid(T theirs)
{
    // Items conflict by guid, let's understand whether there is an item with
    // the same name as theirs in the local storage
    Q_ASSERT(theirs.name());

    QFuture<std::optional<T>> findItemFuture;
    if constexpr (detail::hasLinkedNoteookGuid<T>::value) {
        findItemFuture = std::invoke(
            m_findByNameMemFn, m_localStorage.get(), theirs.name().value(),
            theirs.linkedNotebookGuid());
    }
    else {
        findItemFuture = std::invoke(
            m_findByNameMemFn, m_localStorage.get(), theirs.name().value());
    }

    auto promise = std::make_shared<QPromise<Resolution>>();
    auto future = promise->future();

    promise->start();

    auto thenFuture = threading::then(
        std::move(findItemFuture),
        [promise, theirs = std::move(theirs),
         self_weak = SimpleGenericSyncConflictResolver<
             T, Resolution, FindByNameMemFn>::weak_from_this()](
            std::optional<T> item) mutable {
            auto self = self_weak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError{ErrorString{QStringLiteral(
                        "Cannot resolve sync conflict: "
                        "SimpleGenericSyncConflictResolver instance is "
                        "dead")}});
                promise->finish();
                return;
            }

            if (!item) {
                // There is no conflict by name in the local storage
                promise->addResult(Resolution{
                    ISyncConflictResolver::ConflictResolution::UseTheirs{}});
                promise->finish();
                return;
            }

            // There is a notebook in the local storage which conflicts by name
            // with theirs notebook
            auto future = self->processConflictByName(theirs, std::move(*item));

            auto thenFuture = threading::then(
                std::move(future), [promise](Resolution resolution) {
                    promise->addResult(resolution);
                    promise->finish();
                });

            threading::onFailed(
                std::move(thenFuture), [promise](const QException & e) {
                    promise->setException(e);
                    promise->finish();
                });
        });

    threading::onFailed(
        std::move(thenFuture), [promise](const QException & e) {
            promise->setException(e);
            promise->finish();
        });

    return future;
}

template <class T, class Resolution, class FindByNameMemFn>
QFuture<T> SimpleGenericSyncConflictResolver<
    T, Resolution, FindByNameMemFn>::renameConflictingItem(T item, int counter)
{
    Q_ASSERT(item.name());

    QString newItemName;
    {
        QTextStream strm{&newItemName};
        strm << item.name().value();
        strm << " - ";
        strm << QCoreApplication::translate(
            "synchronization::SimplegenericSyncConflictResolver", "conflicting");

        if (counter > 1) {
            strm << " (";
            strm << counter;
            strm << ")";
        }
    }

    QFuture<std::optional<T>> findItemFuture;
    if constexpr (detail::hasLinkedNoteookGuid<T>::value) {
        findItemFuture = std::invoke(
            m_findByNameMemFn, m_localStorage.get(), newItemName,
            item.linkedNotebookGuid());
    }
    else {
        findItemFuture =
            std::invoke(m_findByNameMemFn, m_localStorage.get(), newItemName);
    }

    if (findItemFuture.isFinished()) {
        try {
            findItemFuture.waitForFinished();
        }
        catch (const QException & e) {
            // Future contains exception, return it directly to the caller
            return threading::makeExceptionalFuture<T>(e);
        }

        if (!findItemFuture.result()) {
            // No conflict by name was found in the local storage, can use
            // the suggested new name
            item.setName(newItemName);
            return threading::makeReadyFuture<T>(std::move(item));
        }

        // Conflict by name was detected, will try once again with another name
        return renameConflictingItem(item, ++counter);
    }

    auto promise = std::make_shared<QPromise<T>>();
    auto future = promise->future();

    promise->start();

    auto watcher = std::make_unique<QFutureWatcher<std::optional<T>>>();
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<qevercloud::Notebook>::finished, rawWatcher,
        [selfWeak = SimpleGenericSyncConflictResolver<
             T, Resolution, FindByNameMemFn>::weak_from_this(),
         promise = std::move(promise), rawWatcher,
         item = std::move(item), newItemName = std::move(newItemName),
         counter = counter]() mutable {
            auto future = rawWatcher->future();
            rawWatcher->deleteLater();

            const auto self = selfWeak.lock();
            if (!self) {
                promise->setException(
                    RuntimeError{ErrorString{QStringLiteral(
                        "Cannot resolve sync conflict: "
                        "SimpleGenericSyncConflictResolver instance is "
                        "dead")}});
                promise->finish();
                return;
            }

            try {
                future.waitForFinished();
            }
            catch (const QException & e) {
                // Failed to check whether item with conflicting name exists
                // in the local storage
                promise->setException(e);
                promise->finish();
                return;
            }

            const auto result = future.result();
            if (!result) {
                // No conflict by name was found in the local storage, can use
                // the suggested new name
                item.setName(newItemName);
                promise->addResult(std::move(item));
                promise->finish();
                return;
            }

            // Conflict by name was detected, will try once again with another
            // name
            auto newFuture = self->renameConflictingItem(item, ++counter);

            auto thenFuture = threading::then(
                std::move(newFuture), [promise](T item) mutable {
                    promise->addResult(std::move(item));
                    promise->finish();
                });

            threading::onFailed(
                std::move(thenFuture), [promise](const QException & e) mutable {
                    promise->setException(e);
                    promise->finish();
                });
        });

    QObject::connect(
        rawWatcher, &QFutureWatcher<qevercloud::Notebook>::canceled, rawWatcher,
        [rawWatcher] { rawWatcher->deleteLater(); });

    watcher->setFuture(findItemFuture);

    Q_UNUSED(watcher.release())
    return future;
}

} // namespace quentier::synchronization
