/*
 * Copyright 2016-2018 Dmitry Ivanov
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

#include "ResourceInfo.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

void ResourceInfo::cacheResourceInfo(const QByteArray & resourceHash,
                                     const QString & resourceDisplayName,
                                     const QString & resourceDisplaySize,
                                     const QString & resourceLocalFilePath,
                                     const QSize & resourceImageSize)
{
    QNDEBUG(QStringLiteral("ResourceInfo::cacheResourceInfo: resource hash = ")
            << resourceHash.toHex()
            << QStringLiteral(", resource display name = ") << resourceDisplayName
            << QStringLiteral(", resource display size = ") << resourceDisplaySize
            << QStringLiteral(", resource local file path = ") << resourceLocalFilePath
            << QStringLiteral(", resource image size = ") << resourceImageSize);

    Info & info = m_resourceInfoHash[resourceHash];
    info.m_resourceDisplayName = resourceDisplayName;
    info.m_resourceDisplaySize = resourceDisplaySize;
    info.m_resourceLocalFilePath = resourceLocalFilePath;
    info.m_resourceImageSize = resourceImageSize;
}

bool ResourceInfo::contains(const QByteArray & resourceHash) const
{
    auto it = m_resourceInfoHash.find(resourceHash);
    return (it != m_resourceInfoHash.end());
}

bool ResourceInfo::findResourceInfo(const QByteArray & resourceHash,
                                    QString & resourceDisplayName,
                                    QString & resourceDisplaySize,
                                    QString & resourceLocalFilePath,
                                    QSize & resourceImageSize) const
{
    QNDEBUG(QStringLiteral("ResourceInfo::findResourceInfo: resource hash = ") << resourceHash.toHex());

    auto it = m_resourceInfoHash.find(resourceHash);
    if (it == m_resourceInfoHash.end()) {
        QNTRACE(QStringLiteral("Resource info was not found"));
        return false;
    }

    const Info & info = it.value();
    resourceDisplayName = info.m_resourceDisplayName;
    resourceDisplaySize = info.m_resourceDisplaySize;
    resourceLocalFilePath = info.m_resourceLocalFilePath;
    resourceImageSize = info.m_resourceImageSize;

    QNTRACE(QStringLiteral("Found resource info: name = ") << resourceDisplayName
            << QStringLiteral(", display size = ") << resourceDisplaySize << QStringLiteral(", local file path = ")
            << resourceLocalFilePath << QStringLiteral(", image size = ") << resourceImageSize);
    return true;
}

bool ResourceInfo::removeResourceInfo(const QByteArray & resourceHash)
{
    QNDEBUG(QStringLiteral("ResourceInfo::removeResourceInfo: resource hash = ") << resourceHash.toHex());

    auto it = m_resourceInfoHash.find(resourceHash);
    if (it == m_resourceInfoHash.end()) {
        QNTRACE(QStringLiteral("Resource info was not found hence not removed"));
        return false;
    }

    Q_UNUSED(m_resourceInfoHash.erase(it));
    return true;
}

void ResourceInfo::clear()
{
    QNDEBUG(QStringLiteral("ResourceInfo::clear"));
    m_resourceInfoHash.clear();
}

} // namespace quentier
