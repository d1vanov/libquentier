/*
 * Copyright 2022 Dmitry Ivanov
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

#include "FullSyncStaleDataExpunger.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/cancelers/ICanceler.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization {

FullSyncStaleDataExpunger::FullSyncStaleDataExpunger(
    local_storage::ILocalStoragePtr localStorage,
    utility::cancelers::ICancelerPtr canceler) :
    m_localStorage{std::move(localStorage)},
    m_canceler{std::move(canceler)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::FullSyncStaleDataExpunger",
            "FullSyncStaleDataExpunger ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_canceler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::FullSyncStaleDataExpunger",
            "FullSyncStaleDataExpunger ctor: canceler is null")}};
    }
}

QFuture<void> FullSyncStaleDataExpunger::expungeStaleData(
    PreservedGuids preservedGuids, // NOLINT
    qevercloud::Guid linkedNotebookGuid) // NOLINT
{
    // TODO: implement
    Q_UNUSED(preservedGuids)
    Q_UNUSED(linkedNotebookGuid)
    return threading::makeReadyFuture();
}

} // namespace quentier::synchronization
