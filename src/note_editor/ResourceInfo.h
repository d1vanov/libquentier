/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <QHash>
#include <QSize>
#include <QString>

namespace quentier {

class ResourceInfo
{
public:
    void cacheResourceInfo(
        const QByteArray & resourceHash, const QString & resourceDisplayName,
        const QString & resourceDisplaySize,
        const QString & resourceLocalFilePath, const QSize & resourceImageSize);

    [[nodiscard]] bool contains(const QByteArray & resourceHash) const noexcept;

    [[nodiscard]] bool findResourceInfo(
        const QByteArray & resourceHash, QString & resourceDisplayName,
        QString & resourceDisplaySize, QString & resourceLocalFilePath,
        QSize & resourceImageSize) const;

    [[nodiscard]] bool removeResourceInfo(const QByteArray & resourceHash);

    void clear();

private:
    struct Info
    {
        QString m_resourceDisplayName;
        QString m_resourceDisplaySize;
        QString m_resourceLocalFilePath;
        QSize m_resourceImageSize;
    };

    using ResourceInfoHash = QHash<QByteArray, Info>;
    ResourceInfoHash m_resourceInfoHash;
};

} // namespace quentier
