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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QString>
#include <QtGlobal>

namespace quentier::enml {

/**
 * @brief The IHtmlData represents the result of ENML to HTML conversion:
 * HTML itself plus some metadata
 */
struct QUENTIER_EXPORT IHtmlData : public Printable
{
    /**
     * HTML representation of note content
     */
    [[nodiscard]] virtual QString html() const = 0;

    /**
     * Number of ToDo nodes within note HTML
     */
    [[nodiscard]] virtual quint32 numEnToDoNodes() const = 0;

    /**
     * Number of hyperlink nodes within note HTML
     */
    [[nodiscard]] virtual quint32 numHyperlinkNodes() const = 0;

    /**
     * Number of en-crypt nodes within note HTML
     */
    [[nodiscard]] virtual quint32 numEnCryptNodes() const = 0;

    /**
     * Number of decrypted en-crypt nodes within note HTML
     */
    [[nodiscard]] virtual quint32 numEnDecryptedNodes() const = 0;

public: // Printable
    QTextStream & print(QTextStream & strm) const override;
};

} // namespace quentier::enml
