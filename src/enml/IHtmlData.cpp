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

#include <quentier/enml/IHtmlData.h>

namespace quentier::enml {

QTextStream & IHtmlData::print(QTextStream & strm) const
{
    strm << "HTML: " << html()
        << "\ntodo nodes: " << numEnToDoNodes()
        << ", hyperlinks: " << numHyperlinkNodes()
        << ", en-crypt nodes: " << numEnCryptNodes()
        << ", decrypted en-crypt nodes: " << numEnDecryptedNodes();
    return strm;
}

} // namespace quentier::enml
