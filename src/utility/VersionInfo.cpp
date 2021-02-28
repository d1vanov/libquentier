/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include <quentier/utility/VersionInfo.h>

#include <qt5qevercloud/VersionInfo.h>

#include <QtGlobal>

namespace quentier {

int libquentierVersionMajor()
{
    return LIB_QUENTIER_VERSION_MAJOR;
}

int libquentierVersionMinor()
{
    return LIB_QUENTIER_VERSION_MINOR;
}

int libquentierVersionPatch()
{
    return LIB_QUENTIER_VERSION_PATCH;
}

QString libquentierBuildInfo()
{
    return QStringLiteral(LIB_QUENTIER_BUILD_INFO);
}

QString libquentierBuiltWithQtVersion()
{
    return QStringLiteral(QT_VERSION_STR);
}

bool libquentierHasNoteEditor()
{
    return static_cast<bool>(LIB_QUENTIER_HAS_NOTE_EDITOR);
}

bool libquentierHasAuthenticationManager()
{
    return static_cast<bool>(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER);
}

bool libquentierUsesQtWebEngine()
{
    return static_cast<bool>(LIB_QUENTIER_USE_QT_WEB_ENGINE);
}

int libquentierBuiltWithQEverCloudVersionMajor()
{
    return QEVERCLOUD_VERSION_MAJOR;
}

int libquentierBuiltWithQEverCloudVersionMinor()
{
    return QEVERCLOUD_VERSION_MINOR;
}

int libquentierBuiltWithQEverCloudVersionPatch()
{
    return QEVERCLOUD_VERSION_PATCH;
}

QString libquentierBuiltWithQEverCloudBuildInfo()
{
    return QStringLiteral(QEVERCLOUD_BUILD_INFO);
}

} // namespace quentier
