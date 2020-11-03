/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_MESSAGE_BOX_H
#define LIB_QUENTIER_UTILITY_MESSAGE_BOX_H

#include <quentier/utility/Linkage.h>

#include <QMessageBox>

namespace quentier {

int QUENTIER_EXPORT genericMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText = {},
    const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);

int QUENTIER_EXPORT informationMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText = {},
    const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);

int QUENTIER_EXPORT warningMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText = {},
    const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);

int QUENTIER_EXPORT criticalMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText = {},
    const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);

int QUENTIER_EXPORT questionMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText = {},
    const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok |
        QMessageBox::Cancel);

/**
 * Convenience function for critical message box due to internal error,
 * has built-in title ("Internal error") and brief text so the caller only
 * needs to provide the detailed text
 */
void QUENTIER_EXPORT
internalErrorMessageBox(QWidget * parent, QString detailedText = {});

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_MESSAGE_BOX_H
