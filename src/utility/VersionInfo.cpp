/*
 * Copyright 2018-2024 Dmitry Ivanov
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

#include <qevercloud/VersionInfo.h>

#include <QtGlobal>

namespace quentier {

int libquentierVersionMajor() noexcept
{
    return LIB_QUENTIER_VERSION_MAJOR;
}

int libquentierVersionMinor() noexcept
{
    return LIB_QUENTIER_VERSION_MINOR;
}

int libquentierVersionPatch() noexcept
{
    return LIB_QUENTIER_VERSION_PATCH;
}

QString libquentierBuildInfo() noexcept
{
    return QStringLiteral(LIB_QUENTIER_BUILD_INFO);
}

QString libquentierBuiltWithQtVersion() noexcept
{
    return QStringLiteral(QT_VERSION_STR);
}

bool libquentierHasNoteEditor() noexcept
{
    return static_cast<bool>(LIB_QUENTIER_HAS_NOTE_EDITOR); // NOLINT
}

bool libquentierHasAuthenticationManager() noexcept
{
    return static_cast<bool>(LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER); // NOLINT
}

int libquentierBuiltWithQEverCloudVersionMajor() noexcept
{
    return QEVERCLOUD_VERSION_MAJOR;
}

int libquentierBuiltWithQEverCloudVersionMinor() noexcept
{
    return QEVERCLOUD_VERSION_MINOR;
}

int libquentierBuiltWithQEverCloudVersionPatch() noexcept
{
    return QEVERCLOUD_VERSION_PATCH;
}

QString libquentierBuiltWithQEverCloudBuildInfo() noexcept
{
    return QStringLiteral(QEVERCLOUD_BUILD_INFO);
}

} // namespace quentier
