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

#include <quentier/local_storage/ILocalStorage.h>

#include <QString>
#include <QTextStream>

class QSqlDatabase;
class QStringList;
class QVariant;

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QString sqlEscape(QString source);

[[nodiscard]] bool rowExists(
    const QString & tableName, const QString & columnName,
    const QVariant & value, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString toQuotedSqlList(const QStringList & items);

template <class T>
[[nodiscard]] QString linkedNotebookGuidSqlQueryCondition(
    const ILocalStorage::ListOptions<T> & options,
    ErrorString & errorDescription)
{
    QString condition;
    if (options.m_affiliation == ILocalStorage::Affiliation::Any) {
        // Do nothing, leave the condition empty
    }
    else if (options.m_affiliation == ILocalStorage::Affiliation::User) {
        condition = QStringLiteral("linkedNotebookGuid IS NULL");
    }
    else if (
        options.m_affiliation == ILocalStorage::Affiliation::AnyLinkedNotebook)
    {
        condition = QStringLiteral("linkedNotebookGuid IS NOT NULL");
    }
    else if (
        options.m_affiliation ==
        ILocalStorage::Affiliation::ParticularLinkedNotebooks)
    {
        if (options.m_linkedNotebookGuids.isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::NotebooksHandler",
                "Detected attempt to list notebooks affiliated with "
                "particular linked notebooks but the list of linked "
                "notebook guids is empty"));
            return {};
        }

        if (options.m_linkedNotebookGuids.size() == 1) {
            condition = QString::fromUtf8("linkedNotebookGuid = '%1'")
                            .arg(utils::sqlEscape(
                                options.m_linkedNotebookGuids.front()));
        }
        else {
            QTextStream strm{&condition};
            strm << "linkedNotebookGuid IN (";
            for (const qevercloud::Guid & linkedNotebookGuid:
                 qAsConst(options.m_linkedNotebookGuids))
            {
                strm << "'" << utils::sqlEscape(linkedNotebookGuid) << "'";
                if (&linkedNotebookGuid !=
                    &options.m_linkedNotebookGuids.back()) {
                    strm << ", ";
                }
            }
            strm << ")";
        }
    }

    return condition;
}

} // namespace quentier::local_storage::sql::utils
