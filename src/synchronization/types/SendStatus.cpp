/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "SendStatus.h"

namespace quentier::synchronization {

namespace {

template <class T>
void printItemWithExceptionList(
    const QList<std::pair<T, ISendStatus::QExceptionPtr>> & values,
    const QString & typeName, QTextStream & strm)
{
    if (values.isEmpty()) {
        strm << "<empty>, ";
        return;
    }

    for (const auto & itemWithException: qAsConst(values)) {
        strm << "{" << typeName << ": " << itemWithException.first
             << "\nException: ";

        if (itemWithException.second) {
            try {
                itemWithException.second->raise();
            }
            catch (const QException & e) {
                strm << e.what();
            }
        }
        else {
            strm << "<no exception info>";
        }

        strm << "};";
    }
    strm << " ";
}

} // namespace

quint64 SendStatus::totalAttemptedToSendNotes() const noexcept
{
    return m_totalAttemptedToSendNotes;
}

quint64 SendStatus::totalAttemptedToSendNotebooks() const noexcept
{
    return m_totalAttemptedToSendNotebooks;
}

quint64 SendStatus::totalAttemptedToSendSavedSearches() const noexcept
{
    return m_totalAttemptedToSendSavedSearches;
}

quint64 SendStatus::totalAttemptedToSendTags() const noexcept
{
    return m_totalAttemptedToSendTags;
}

quint64 SendStatus::totalSuccessfullySentNotes() const noexcept
{
    return m_totalSuccessfullySentNotes;
}

QList<ISendStatus::NoteWithException> SendStatus::failedToSendNotes() const
{
    return m_failedToSendNotes;
}

quint64 SendStatus::totalSuccessfullySentNotebooks() const noexcept
{
    return m_totalSuccessfullySentNotebooks;
}

QList<ISendStatus::NotebookWithException> SendStatus::failedToSendNotebooks()
    const
{
    return m_failedToSendNotebooks;
}

quint64 SendStatus::totalSuccessfullySentSavedSearches() const noexcept
{
    return m_totalSuccessfullySentSavedSearches;
}

QList<ISendStatus::SavedSearchWithException>
    SendStatus::failedToSendSavedSearches() const
{
    return m_failedToSendSavedSearches;
}

quint64 SendStatus::totalSuccessfullySentTags() const noexcept
{
    return m_totalSuccessfullySentTags;
}

QList<ISendStatus::TagWithException> SendStatus::failedToSendTags() const
{
    return m_failedToSendTags;
}

StopSynchronizationError SendStatus::stopSynchronizationError() const
{
    return m_stopSynchronizationError;
}

bool SendStatus::needToRepeatIncrementalSync() const noexcept
{
    return m_needToRepeatIncrementalSync;
}

QTextStream & SendStatus::print(QTextStream & strm) const
{
    strm << "SendStatus: total attempted to send notes = "
         << m_totalAttemptedToSendNotes
         << ", total attempted to send notebooks: "
         << m_totalAttemptedToSendNotebooks
         << ", total attempted to send saved searches: "
         << m_totalAttemptedToSendSavedSearches
         << ", total attempted to send tags: " << m_totalAttemptedToSendTags
         << ", total successfully sent notes: " << m_totalSuccessfullySentNotes;

    strm << ", notes which failed to send: ";
    printItemWithExceptionList<qevercloud::Note>(
        m_failedToSendNotes, QStringLiteral("note"), strm);

    strm << "total successfully sent notebooks: "
         << m_totalSuccessfullySentNotebooks << ", failed to send notebooks: ";
    printItemWithExceptionList<qevercloud::Notebook>(
        m_failedToSendNotebooks, QStringLiteral("notebook"), strm);

    strm << "total successfully sent saved searches: "
         << m_totalSuccessfullySentSavedSearches
         << ", failed to send saved searches: ";
    printItemWithExceptionList<qevercloud::SavedSearch>(
        m_failedToSendSavedSearches, QStringLiteral("savedSearch"), strm);

    strm << "total successfully sent tags: " << m_totalSuccessfullySentTags
         << ", failed to send tags: ";
    printItemWithExceptionList<qevercloud::Tag>(
        m_failedToSendTags, QStringLiteral("tag"), strm);

    if (std::holds_alternative<RateLimitReachedError>(
            m_stopSynchronizationError)) {
        const auto & rateLimitReachedError =
            std::get<RateLimitReachedError>(m_stopSynchronizationError);
        strm << "stopSynchronizationError = RateLimitReachedError{";
        if (rateLimitReachedError.rateLimitDurationSec) {
            strm << "duration = "
                 << *rateLimitReachedError.rateLimitDurationSec;
        }
        strm << "}";
    }
    else if (std::holds_alternative<AuthenticationExpiredError>(
                 m_stopSynchronizationError))
    {
        strm << "stopSynchronizationError = AuthenticationExpiredError";
    }

    strm << "need to repeat incremental sync: "
         << (m_needToRepeatIncrementalSync ? "true" : "false");

    return strm;
}

} // namespace quentier::synchronization
