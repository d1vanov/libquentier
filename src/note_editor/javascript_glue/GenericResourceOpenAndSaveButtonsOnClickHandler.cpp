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

#include "GenericResourceOpenAndSaveButtonsOnClickHandler.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

GenericResourceOpenAndSaveButtonsOnClickHandler::
    GenericResourceOpenAndSaveButtonsOnClickHandler(QObject * parent) :
    QObject(parent)
{}

void GenericResourceOpenAndSaveButtonsOnClickHandler::
    onOpenResourceButtonPressed(const QString & resourceHash)
{
    QNDEBUG(
        "note_editor:js_glue",
        "GenericResourceOpenAndSaveButtonsOnClickHandler"
            << "::onOpenResourceButtonPressed: " << resourceHash);

    Q_EMIT openResourceRequest(QByteArray::fromHex(resourceHash.toLocal8Bit()));
}

void GenericResourceOpenAndSaveButtonsOnClickHandler::
    onSaveResourceButtonPressed(const QString & resourceHash)
{
    QNDEBUG(
        "note_editor:js_glue",
        "GenericResourceOpenAndSaveButtonsOnClickHandler"
            << "::onSaveResourceButtonPressed: " << resourceHash);

    Q_EMIT saveResourceRequest(QByteArray::fromHex(resourceHash.toLocal8Bit()));
}

} // namespace quentier
