/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "GenericResourceImageJavaScriptHandler.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

GenericResourceImageJavaScriptHandler::GenericResourceImageJavaScriptHandler(
    const QHash<QByteArray, QString> & cache, QObject * parent) :
    QObject(parent),
    m_cache(cache)
{}

void GenericResourceImageJavaScriptHandler::findGenericResourceImage(
    QByteArray resourceHash)
{
    QNDEBUG(
        "note_editor:js_glue",
        "GenericResourceImageJavaScriptHandler"
            << "::findGenericResourceImage: resource hash = " << resourceHash);

    auto it = m_cache.find(QByteArray::fromHex(resourceHash));
    if (it != m_cache.end()) {
        QNTRACE(
            "note_editor:js_glue",
            "Found generic resouce image, path is " << it.value());
        Q_EMIT genericResourceImageFound(resourceHash, it.value());
    }
    else {
        QNINFO(
            "note_editor:js_glue",
            "Can't find generic resource image for "
                << "hash " << resourceHash);
    }
}

} // namespace quentier
