/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncConflictResolver.h>

#include <QDebug>
#include <QTextStream>

namespace quentier::synchronization {

namespace {

template <class Stream, class Resolution>
void printConflictResolution(Stream & strm, const Resolution & resolution)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;

    struct Visitor
    {
        Visitor(Stream & strm) : m_strm{strm} {};

        void operator()(const ConflictResolution::UseTheirs &)
        {
            m_strm << "Use theirs";
        }

        void operator()(const ConflictResolution::UseMine &)
        {
            m_strm << "Use mine";
        }

        void operator()(const ConflictResolution::IgnoreMine &)
        {
            m_strm << "Ignore mine";
        }

        void operator()(
            const ConflictResolution::MoveMine<qevercloud::Notebook> &)
        {
            m_strm << "Move mine (notebook)";
        }

        void operator()(const ConflictResolution::MoveMine<qevercloud::Note> &)
        {
            m_strm << "Move mine (note)";
        }

        void operator()(
            const ConflictResolution::MoveMine<qevercloud::SavedSearch> &)
        {
            m_strm << "Move mine (saved search)";
        }

        void operator()(const ConflictResolution::MoveMine<qevercloud::Tag> &)
        {
            m_strm << "Move mine (tag)";
        }

        Stream & m_strm;
    };

    std::visit(Visitor{strm}, resolution);
}

} // namespace

ISyncConflictResolver::~ISyncConflictResolver() noexcept = default;

QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::NotebookConflictResolution & resolution)
{
    printConflictResolution(dbg, resolution);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::NotebookConflictResolution & resolution)
{
    printConflictResolution(strm, resolution);
    return strm;
}

QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::NoteConflictResolution & resolution)
{
    printConflictResolution(dbg, resolution);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::NoteConflictResolution & resolution)
{
    printConflictResolution(strm, resolution);
    return strm;
}

QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::SavedSearchConflictResolution & resolution)
{
    printConflictResolution(dbg, resolution);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::SavedSearchConflictResolution & resolution)
{
    printConflictResolution(strm, resolution);
    return strm;
}

QDebug & operator<<(
    QDebug & dbg,
    const ISyncConflictResolver::TagConflictResolution & resolution)
{
    printConflictResolution(dbg, resolution);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm,
    const ISyncConflictResolver::TagConflictResolution & resolution)
{
    printConflictResolution(strm, resolution);
    return strm;
}

} // namespace quentier::synchronization
