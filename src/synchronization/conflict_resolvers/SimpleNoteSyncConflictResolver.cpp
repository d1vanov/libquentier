/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include "SimpleNoteSyncConflictResolver.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/types/NoteUtils.h>

#include <QCoreApplication>

namespace quentier::synchronization {

QFuture<ISyncConflictResolver::NoteConflictResolution>
    SimpleNoteSyncConflictResolver::resolveNoteConflict(
        qevercloud::Note theirs, qevercloud::Note mine)
{
    QNDEBUG(
        "synchronization::SimpleNoteSyncConflictResolver",
        "SimpleNoteSyncConflictResolver::resolveNoteConflict: theirs: "
            << "guid = " << theirs.guid().value_or(QStringLiteral("<not set>"))
            << ", update sequence number = "
            << (theirs.updateSequenceNum()
                    ? QString::number(*theirs.updateSequenceNum())
                    : QStringLiteral("<not set>"))
            << ", mine: local id = " << mine.localId()
            << ", guid = " << mine.guid().value_or(QStringLiteral("<not set>"))
            << ", update sequence number = "
            << (mine.updateSequenceNum()
                    ? QString::number(*mine.updateSequenceNum())
                    : QStringLiteral("<not set>")));

    using Resolution = ISyncConflictResolver::NoteConflictResolution;

    if (Q_UNLIKELY(!theirs.guid())) {
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{ErrorString{QStringLiteral(
                "Cannot resolve sync conflict: remote note has no guid")}});
    }

    if (Q_UNLIKELY(!theirs.updateSequenceNum())) {
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{ErrorString{QStringLiteral(
                "Cannot resolve sync conflict: remote note has no update "
                "sequence number")}});
    }

    if (Q_UNLIKELY(!mine.guid())) {
        return threading::makeExceptionalFuture<Resolution>(
            InvalidArgument{ErrorString{QStringLiteral(
                "Cannot resolve sync conflict: local note has no guid")}});
    }

    if (Q_UNLIKELY(*mine.guid() != *theirs.guid())) {
        return threading::makeReadyFuture<Resolution>(Resolution{
            ISyncConflictResolver::ConflictResolution::IgnoreMine{}});
    }

    if (mine.updateSequenceNum() &&
        (*mine.updateSequenceNum() >= *theirs.updateSequenceNum()))
    {
        return threading::makeReadyFuture<Resolution>(
            Resolution{ISyncConflictResolver::ConflictResolution::UseMine{}});
    }

    if (!mine.isLocallyModified()) {
        QNDEBUG(
            "synchronization::SimpleNoteSyncConflictResolver",
            "Mine note is not modified => it should be overridden by theirs");
        return threading::makeReadyFuture<Resolution>(
            Resolution{ISyncConflictResolver::ConflictResolution::UseTheirs{}});
    }

    QNDEBUG(
        "synchronization::SimpleNoteSyncConflictResolver",
        "Mine note should be considered a local conflicting note");

    markAsLocalConflictingNote(*theirs.guid(), mine);
    return threading::makeReadyFuture<Resolution>(Resolution{
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Note>{
            std::move(mine)}});
}

void SimpleNoteSyncConflictResolver::markAsLocalConflictingNote(
    qevercloud::Guid theirsGuid, qevercloud::Note & mine) const
{
    mine.setGuid(std::nullopt);
    mine.setUpdateSequenceNum(std::nullopt);

    if (!mine.attributes()) {
        mine.setAttributes(qevercloud::NoteAttributes{});
    }

    mine.mutableAttributes()->setConflictSourceNoteGuid(std::move(theirsGuid));
    mine.setTitle(utils::makeLocalConflictingNoteTitle(mine));

    if (mine.resources() && !mine.resources()->isEmpty()) {
        for (auto & resource: *mine.mutableResources()) {
            resource.setGuid(std::nullopt);
            resource.setNoteGuid(std::nullopt);
            resource.setUpdateSequenceNum(std::nullopt);
            resource.setLocallyModified(true);
        }
    }
}

} // namespace quentier::synchronization
