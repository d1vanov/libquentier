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

#pragma once

#include <memory>

namespace quentier::synchronization {

struct DownloadNotesStatus;
using DownloadNotesStatusPtr = std::shared_ptr<DownloadNotesStatus>;

struct DownloadResourcesStatus;
using DownloadResourcesStatusPtr = std::shared_ptr<DownloadResourcesStatus>;

struct SendStatus;
using SendStatusPtr = std::shared_ptr<SendStatus>;

struct SyncResult;
using SyncResultPtr = std::shared_ptr<SyncResult>;
using SyncResultConstPtr = std::shared_ptr<const SyncResult>;

struct SyncState;
using SyncStatePtr = std::shared_ptr<SyncState>;
using SyncStateConstPtr = std::shared_ptr<const SyncState>;

} // namespace quentier::synchronization
