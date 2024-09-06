/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "Fwd.h"
#include "IInkNoteImageDownloaderFactory.h"

#include <quentier/types/Account.h>

#include <synchronization/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class InkNoteImageDownloaderFactory final :
    public IInkNoteImageDownloaderFactory,
    public std::enable_shared_from_this<InkNoteImageDownloaderFactory>
{
public:
    InkNoteImageDownloaderFactory(
        Account account,
        IAuthenticationInfoProviderPtr authenticationInfoProvider,
        ILinkedNotebookFinderPtr linkedNotebookFinder);

public: // IInkNoteImageDownloaderFactory
    [[nodiscard]] QFuture<qevercloud::IInkNoteImageDownloaderPtr>
        createInkNoteImageDownloader(
            qevercloud::Guid notebookGuid,
            qevercloud::IRequestContextPtr ctx = {}) override;

private:
    void createUserOwnInkNoteImageDownloader(
        const std::shared_ptr<
            QPromise<qevercloud::IInkNoteImageDownloaderPtr>> & promise,
        qevercloud::IRequestContextPtr ctx);

    void createLinkedNotebookInkNoteImageDownloader(
        const std::shared_ptr<
            QPromise<qevercloud::IInkNoteImageDownloaderPtr>> & promise,
        qevercloud::LinkedNotebook linkedNotebook,
        qevercloud::IRequestContextPtr ctx);

private:
    const Account m_account;
    const IAuthenticationInfoProviderPtr m_authenticationInfoProvider;
    const ILinkedNotebookFinderPtr m_linkedNotebookFinder;
};

} // namespace quentier::synchronization
