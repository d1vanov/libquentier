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

#pragma once

#include <quentier/types/ErrorString.h>
#include <quentier/types/Result.h>
#include <quentier/utility/Linkage.h>

#include <QFlags>

#include <memory>

namespace quentier::enml::utils {

[[nodiscard]] Result<QString, ErrorString> QUENTIER_EXPORT convertHtmlToXml(
    const QString & html);

[[nodiscard]] Result<QString, ErrorString> QUENTIER_EXPORT convertHtmlToXhtml(
    const QString & html);

[[nodiscard]] Result<QString, ErrorString> QUENTIER_EXPORT cleanupHtml(const QString & html);

enum class EscapeStringOption
{
    Simplify = 1 << 0,
};

Q_DECLARE_FLAGS(EscapeStringOptions, EscapeStringOption);

[[nodiscard]] QString QUENTIER_EXPORT htmlEscapeString(
    QString str, EscapeStringOptions options);

} // namespace quentier::enml::utils
