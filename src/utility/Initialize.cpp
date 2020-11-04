/*
 * Copyright 2020 Dmitry Ivanov
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

#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/Initialize.h>
#include <quentier/utility/VersionInfo.h>

#if LIB_QUENTIER_HAS_NOTE_EDITOR
#include "../note_editor/NoteEditorLocalStorageBroker.h"
#endif

#include <qt5qevercloud/QEverCloud.h>

#include <QCoreApplication>

namespace quentier {

void initializeLibquentier()
{
    qevercloud::initializeQEverCloud();

    registerMetatypes();

#if LIB_QUENTIER_HAS_NOTE_EDITOR
    // Ensure the instance is created now and not later
    Q_UNUSED(NoteEditorLocalStorageBroker::instance())
#endif

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    // Attempt to workaround https://bugreports.qt.io/browse/QTBUG-40765
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
#endif
}

} // namespace quentier
