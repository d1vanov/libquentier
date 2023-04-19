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

class IAuthenticationInfo;
using IAuthenticationInfoPtr = std::shared_ptr<IAuthenticationInfo>;

class IDownloadNotesStatus;
using IDownloadNotesStatusPtr = std::shared_ptr<IDownloadNotesStatus>;

class IDownloadResourcesStatus;
using IDownloadResourcesStatusPtr = std::shared_ptr<IDownloadResourcesStatus>;

class ISendStatus;
using ISendStatusPtr = std::shared_ptr<ISendStatus>;

class ISyncOptions;
using ISyncOptionsPtr = std::shared_ptr<ISyncOptions>;

class ISyncOptionsBuilder;
using ISyncOptionsBuilderPtr = std::shared_ptr<ISyncOptionsBuilder>;

class ISyncResult;
using ISyncResultPtr = std::shared_ptr<ISyncResult>;

class ISyncState;
using ISyncStatePtr = std::shared_ptr<ISyncState>;

} // namespace quentier::synchronization
