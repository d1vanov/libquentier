/*
 * Copyright 2020-2023 Dmitry Ivanov
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

#include <quentier/enml/Factory.h>
#include <quentier/enml/IConverter.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/NoteUtils.h>

#include <qevercloud/types/Note.h>

#include <QXmlStreamReader>

#include <algorithm>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] bool noteContentContainsToDoImpl(
    const QString & noteContent, const bool checked)
{
    QXmlStreamReader reader(noteContent);

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartElement() &&
            (reader.name() == QStringLiteral("en-todo"))) {
            const QXmlStreamAttributes attributes = reader.attributes();
            if (checked && attributes.hasAttribute(QStringLiteral("checked")) &&
                (attributes.value(QStringLiteral("checked")) ==
                 QStringLiteral("true")))
            {
                return true;
            }

            if (!checked &&
                (!attributes.hasAttribute(QStringLiteral("checked")) ||
                 (attributes.value(QStringLiteral("checked")) ==
                  QStringLiteral("false"))))
            {
                return true;
            }
        }
    }

    return false;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

bool isInkNote(const qevercloud::Note & note)
{
    if (!note.resources()) {
        return false;
    }

    const auto & resources = *note.resources();

    // NOTE: it is not known for sure how many resources there might be within
    // an ink note. Probably just one in most cases.
    const int numResources = resources.size();
    if (numResources == 0) {
        return false;
    }

    for (int i = 0; i < numResources; ++i) {
        const auto & resource = resources[i];
        if (!resource.mime()) {
            return false;
        }

        if (*resource.mime() != QStringLiteral("application/vnd.evernote.ink"))
        {
            return false;
        }
    }

    return true;
}

bool noteContentContainsCheckedToDo(const QString & noteContent)
{
    return noteContentContainsToDoImpl(noteContent, /* checked = */ true);
}

bool noteContentContainsUncheckedToDo(const QString & noteContent)
{
    return noteContentContainsToDoImpl(noteContent, /* checked = */ false);
}

bool noteContentContainsToDo(const QString & noteContent)
{
    return noteContentContainsCheckedToDo(noteContent) ||
        noteContentContainsUncheckedToDo(noteContent);
}

bool noteContentContainsEncryptedFragments(const QString & noteContent)
{
    QXmlStreamReader reader(noteContent);
    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartElement() &&
            (reader.name() == QStringLiteral("en-crypt"))) {
            return true;
        }
    }

    return false;
}

QString noteContentToPlainText(
    const QString & noteContent, ErrorString * errorDescription)
{
    auto converter = enml::createConverter();
    auto res = converter->convertEnmlToPlainText(noteContent);
    if (!res.isValid()) {
        const auto & error = res.error();
        QNWARNING("types:note_utils", error);
        if (errorDescription) {
            *errorDescription = error;
        }

        return {};
    }

    return res.get();
}

QStringList noteContentToListOfWords(
    const QString & noteContent, ErrorString * errorDescription)
{
    auto converter = enml::createConverter();
    auto res = converter->convertEnmlToWordsList(noteContent);
    if (!res.isValid()) {
        const auto & error = res.error();
        QNWARNING("types:note_utils", error);
        if (errorDescription) {
            *errorDescription = error;
        }

        return {};
    }

    return res.get();
}

std::pair<QString, QStringList> noteContentToPlainTextAndListOfWords(
    const QString & noteContent, ErrorString * errorDescription)
{
    auto converter = enml::createConverter();

    auto res = converter->convertEnmlToPlainText(noteContent);
    if (!res.isValid()) {
        const auto & error = res.error();
        QNWARNING("types:note_utils", error);
        if (errorDescription) {
            *errorDescription = error;
        }

        return {};
    }

    auto wordsList = converter->convertPlainTextToWordsList(res.get());
    return std::pair{std::move(res.get()), std::move(wordsList)};
}

} // namespace quentier
