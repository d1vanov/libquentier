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

#include <utility>

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

    for (const auto & itemWithException: std::as_const(values)) {
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
            m_stopSynchronizationError))
    {
        const auto & rateLimitReachedError =
            std::get<RateLimitReachedError>(m_stopSynchronizationError);
        strm << "stopSynchronizationError = RateLimitReachedError{";
        if (rateLimitReachedError.rateLimitDurationSec) {
            strm << "duration = "
                 << *rateLimitReachedError.rateLimitDurationSec;
        }
        strm << "}, ";
    }
    else if (std::holds_alternative<AuthenticationExpiredError>(
                 m_stopSynchronizationError))
    {
        strm << "stopSynchronizationError = AuthenticationExpiredError, ";
    }

    strm << "need to repeat incremental sync: "
         << (m_needToRepeatIncrementalSync ? "true" : "false");

    return strm;
}

bool operator==(const SendStatus & lhs, const SendStatus & rhs) noexcept
{
    const auto compareExceptionPtrs =
        [](const ISendStatus::QExceptionPtr & lhs,
           const ISendStatus::QExceptionPtr & rhs) {
            if (!lhs && !rhs) {
                return true;
            }

            if (lhs && !rhs) {
                return false;
            }

            if (!lhs && rhs) {
                return false;
            }

            const auto lhsMsg = QString::fromUtf8(lhs->what());
            const auto rhsMsg = QString::fromUtf8(rhs->what());
            return lhsMsg == rhsMsg;
        };

    const auto compareItemsWithExceptions = [&](const auto & lhs,
                                                const auto & rhs) {
        return lhs.first == rhs.first &&
            compareExceptionPtrs(lhs.second, rhs.second);
    };

    const auto compareItemListsWithExceptions = [&](const auto & lhs,
                                                    const auto & rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (int i = 0; i < lhs.size(); ++i) {
            if (!compareItemsWithExceptions(lhs[i], rhs[i])) {
                return false;
            }
        }

        return true;
    };

    return (lhs.m_totalAttemptedToSendNotes ==
            rhs.m_totalAttemptedToSendNotes) &&
        (lhs.m_totalAttemptedToSendNotebooks ==
         rhs.m_totalAttemptedToSendNotebooks) &&
        (lhs.m_totalAttemptedToSendSavedSearches ==
         rhs.m_totalAttemptedToSendSavedSearches) &&
        (lhs.m_totalAttemptedToSendTags == rhs.m_totalAttemptedToSendTags) &&
        (lhs.m_totalSuccessfullySentNotes ==
         rhs.m_totalSuccessfullySentNotes) &&
        compareItemListsWithExceptions(
               lhs.m_failedToSendNotes, rhs.m_failedToSendNotes) &&
        (lhs.m_totalSuccessfullySentNotebooks ==
         rhs.m_totalSuccessfullySentNotebooks) &&
        compareItemListsWithExceptions(
               lhs.m_failedToSendNotebooks, rhs.m_failedToSendNotebooks) &&
        (lhs.m_totalSuccessfullySentSavedSearches ==
         rhs.m_totalSuccessfullySentSavedSearches) &&
        compareItemListsWithExceptions(
               lhs.m_failedToSendSavedSearches,
               rhs.m_failedToSendSavedSearches) &&
        (lhs.m_totalSuccessfullySentTags == rhs.m_totalSuccessfullySentTags) &&
        compareItemListsWithExceptions(
               lhs.m_failedToSendTags, rhs.m_failedToSendTags) &&
        (lhs.m_stopSynchronizationError == rhs.m_stopSynchronizationError) &&
        (lhs.m_needToRepeatIncrementalSync ==
         rhs.m_needToRepeatIncrementalSync);
}

bool operator!=(const SendStatus & lhs, const SendStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
