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

#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_RESOURCE_INFO_JAVASCRIPT_HANDLER_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_RESOURCE_INFO_JAVASCRIPT_HANDLER_H

#include <QObject>
#include <QSize>
#include <QString>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceInfo)

/**
 * The ResourceInfoJavaScriptHandler is used for communicating the information
 * on resources from C++ to JavaScript on requests coming from JavaScript to C++
 */
class ResourceInfoJavaScriptHandler final : public QObject
{
    Q_OBJECT
public:
    explicit ResourceInfoJavaScriptHandler(
        const ResourceInfo & resourceInfo, QObject * parent = nullptr);

Q_SIGNALS:
    void notifyResourceInfo(
        const QString & resourceHash, const QString & resourceLocalFilePath,
        const QString & resourceDisplayName,
        const QString & resourceDisplaySize, const int resourceImageHeight,
        const int resourceImageWidth);

public Q_SLOTS:
    void findResourceInfo(const QString & resourceHash);

private:
    const ResourceInfo & m_resourceInfo;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_RESOURCE_INFO_JAVASCRIPT_HANDLER_H
