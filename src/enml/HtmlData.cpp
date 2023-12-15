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

#include "HtmlData.h"

namespace quentier::enml {

QString HtmlData::html() const
{
    return m_html;
}

quint32 HtmlData::numEnToDoNodes() const noexcept
{
    return m_enToDoNodes;
}

quint32 HtmlData::numHyperlinkNodes() const noexcept
{
    return m_hyperlinkNodes;
}

quint32 HtmlData::numEnCryptNodes() const noexcept
{
    return m_enCryptNodes;
}

quint32 HtmlData::numEnDecryptedNodes() const noexcept
{
    return m_enDecryptedNodes;
}

} // namespace quentier::enml
