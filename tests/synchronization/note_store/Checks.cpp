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

#include "Checks.h"
#include "../utils/ExceptionUtils.h"

#include <qevercloud/Constants.h>
#include <qevercloud/exceptions/builders/EDAMUserExceptionBuilder.h>
#include <qevercloud/types/Notebook.h>

#include <QRegularExpression>

namespace quentier::synchronization::tests::note_store {

std::optional<qevercloud::EDAMUserException> checkNotebook(
    const qevercloud::Notebook & notebook)
{
    if (!notebook.name()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    const QString & notebookName = *notebook.name();
    if (notebookName.size() < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebookName.size() > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebookName != notebookName.trimmed()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    static const QRegularExpression notebookNameRegex{
        qevercloud::EDAM_NOTEBOOK_NAME_REGEX};

    if (!notebookNameRegex.match(notebookName).hasMatch()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebook.stack()) {
        const QString & notebookStack = *notebook.stack();

        if (notebookStack.size() < qevercloud::EDAM_NOTEBOOK_STACK_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        if (notebookStack.size() > qevercloud::EDAM_NOTEBOOK_STACK_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        if (notebookStack != notebookStack.trimmed()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        static const QRegularExpression notebookStackRegex{
            qevercloud::EDAM_NOTEBOOK_STACK_REGEX};

        if (!notebookStackRegex.match(notebookStack).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }
    }

    if (notebook.published() && notebook.publishing()->uri()) {
        const QString & publishingUri = *notebook.publishing()->uri();

        if (publishingUri.size() < qevercloud::EDAM_PUBLISHING_URI_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }

        if (publishingUri.size() > qevercloud::EDAM_PUBLISHING_URI_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }

        for (const auto & it: qevercloud::EDAM_PUBLISHING_URI_PROHIBITED) {
            if (publishingUri == it) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                    QStringLiteral("Publishing.uri"));
            }
        }

        static const QRegularExpression publishingUriRegExp{
            qevercloud::EDAM_PUBLISHING_URI_REGEX};

        if (!publishingUriRegExp.match(publishingUri).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }
    }

    if (notebook.publishing() && notebook.publishing()->publicDescription()) {
        const QString & description =
            *notebook.publishing()->publicDescription();

        if (description.size() <
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }

        if (description.size() >
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }

        static const QRegularExpression publishingDescriptionRegExp{
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_REGEX};

        if (!publishingDescriptionRegExp.match(description).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }
    }

    return std::nullopt;
}

} // namespace quentier::synchronization::tests::note_store
