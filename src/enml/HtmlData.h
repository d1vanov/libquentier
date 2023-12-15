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

#include <quentier/enml/IHtmlData.h>

namespace quentier::enml {

struct HtmlData: public IHtmlData
{
public: // IHtmlData
    [[nodiscard]] QString html() const override;
    [[nodiscard]] quint32 numEnToDoNodes() const noexcept override;
    [[nodiscard]] quint32 numHyperlinkNodes() const noexcept override;
    [[nodiscard]] quint32 numEnCryptNodes() const noexcept override;
    [[nodiscard]] quint32 numEnDecryptedNodes() const noexcept override;

    QString m_html;
    quint32 m_enToDoNodes = 0;
    quint32 m_hyperlinkNodes = 0;
    quint32 m_enCryptNodes = 0;
    quint32 m_enDecryptedNodes;
};

} // namespace quentier::enml
