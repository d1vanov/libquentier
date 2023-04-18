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

#include <synchronization/ISender.h>

#include <gmock/gmock.h>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests::mocks {

class MockISender : public ISender
{
public:
    MOCK_METHOD(
        QFuture<ISender::Result>, send,
        (utility::cancelers::ICancelerPtr canceler,
         ISender::ICallbackWeakPtr callbackWeak),
        (override));
};

class MockISenderICallback : public ISender::ICallback
{
public:
    MOCK_METHOD(
        void, onUserOwnSendStatusUpdate, (SendStatusPtr sendStatus),
        (override));

    MOCK_METHOD(
        void, onLinkedNotebookSendStatusUpdate,
        (const qevercloud::Guid & linkedNotebookGuid,
         SendStatusPtr sendStatus),
        (override));
};

} // namespace quentier::synchronization::tests::mocks
