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

#include "ResourceInfoJavaScriptHandler.h"

#include "../ResourceInfo.h"

namespace quentier {

ResourceInfoJavaScriptHandler::ResourceInfoJavaScriptHandler(
    const ResourceInfo & resourceInfo, QObject * parent) :
    QObject(parent),
    m_resourceInfo(resourceInfo)
{}

void ResourceInfoJavaScriptHandler::findResourceInfo(
    const QString & resourceHash)
{
    QString resourceDisplayName;
    QString resourceDisplaySize;
    QString resourceLocalFilePath;
    QSize resourceImageSize;

    bool found = m_resourceInfo.findResourceInfo(
        QByteArray::fromHex(resourceHash.toLocal8Bit()), resourceDisplayName,
        resourceDisplaySize, resourceLocalFilePath, resourceImageSize);

    if (found) {
        bool resourceImageSizeValid = resourceImageSize.isValid();
        int height = resourceImageSizeValid ? resourceImageSize.height() : 0;
        int width = resourceImageSizeValid ? resourceImageSize.width() : 0;

        Q_EMIT notifyResourceInfo(
            resourceHash, resourceLocalFilePath, resourceDisplayName,
            resourceDisplaySize, height, width);
    }
}

} // namespace quentier
