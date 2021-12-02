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

#pragma once

#include <memory>

class QReadWriteLock;
class QSqlDatabase;
class QThread;

namespace quentier::local_storage::sql {

class ConnectionPool;
using ConnectionPoolPtr = std::shared_ptr<ConnectionPool>;

class ILinkedNotebooksHandler;
using ILinkedNotebooksHandlerPtr = std::shared_ptr<ILinkedNotebooksHandler>;

class INotebooksHandler;
using INotebooksHandlerPtr = std::shared_ptr<INotebooksHandler>;

class INotesHandler;
using INotesHandlerPtr = std::shared_ptr<INotesHandler>;

class IResourcesHandler;
using IResourcesHandlerPtr = std::shared_ptr<IResourcesHandler>;

class ISavedSearchesHandler;
using ISavedSearchesHandlerPtr = std::shared_ptr<ISavedSearchesHandler>;

class ISynchronizationInfoHandler;
using ISynchronizationInfoHandlerPtr = std::shared_ptr<ISynchronizationInfoHandler>;

class ITagsHandler;
using ITagsHandlerPtr = std::shared_ptr<ITagsHandler>;

class IVersionHandler;
using IVersionHandlerPtr = std::shared_ptr<IVersionHandler>;

class Notifier;

class Transaction;

struct TaskContext;

using QReadWriteLockPtr = std::shared_ptr<QReadWriteLock>;

using QThreadPtr = std::shared_ptr<QThread>;

} // namespace quentier::local_storage::sql
