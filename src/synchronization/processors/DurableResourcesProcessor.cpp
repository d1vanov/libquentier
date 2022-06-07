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

#include "DurableResourcesProcessor.h"

#include <synchronization/processors/IResourcesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <algorithm>

namespace quentier::synchronization {

DurableResourcesProcessor::DurableResourcesProcessor(
    IResourcesProcessorPtr resourcesProcessor,
    const QDir & syncPersistentStorageDir) :
    m_resourcesProcessor{std::move(resourcesProcessor)},
    m_syncNotesDir{[&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{lastSyncDataDir.absoluteFilePath(
            QStringLiteral("resources"))};
    }()}
{
    if (Q_UNLIKELY(!m_resourcesProcessor)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::DurableResourcesProcessor",
            "DurableResourcesProcessor ctor: resources processor is null")}};
    }
}

} // namespace quentier::synchronization
