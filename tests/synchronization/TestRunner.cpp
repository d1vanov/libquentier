/*
 * Copyright 2023 Dmitry Ivanov
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

#include "TestRunner.h"

#include "FakeAuthenticator.h"
#include "FakeKeychainService.h"
#include "FakeSyncStateStorage.h"
#include "NoteStoreServer.h"
#include "SyncEventsCollector.h"
#include "UserStoreServer.h"

namespace quentier::synchronization::tests {

TestRunner::TestRunner(QObject * parent) : QObject(parent) {}

TestRunner::~TestRunner() = default;

} // namespace quentier::synchronization::tests
