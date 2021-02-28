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

#include <quentier/utility/MessageBox.h>

#include <QApplication>

#include <memory>

namespace quentier {

int messageBoxImplementation(
    const QMessageBox::Icon icon, QWidget * parent, const QString & title,
    const QString & briefText, const QString & detailedText,
    QMessageBox::StandardButtons buttons)
{
    auto pMessageBox = std::make_unique<QMessageBox>(parent);
    if (parent) {
        pMessageBox->setWindowModality(Qt::WindowModal);
    }

    pMessageBox->setWindowTitle(
        QApplication::applicationName() + QStringLiteral(" - ") + title);

    pMessageBox->setText(briefText);
    if (!detailedText.isEmpty()) {
        pMessageBox->setInformativeText(detailedText);
    }

    pMessageBox->setIcon(icon);
    pMessageBox->setStandardButtons(buttons);
    return pMessageBox->exec();
}

int genericMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText, const QMessageBox::StandardButtons buttons)
{
    return messageBoxImplementation(
        QMessageBox::NoIcon, parent, title, briefText, detailedText, buttons);
}

int informationMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText, const QMessageBox::StandardButtons buttons)
{
    return messageBoxImplementation(
        QMessageBox::Information, parent, title, briefText, detailedText,
        buttons);
}

int warningMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText, const QMessageBox::StandardButtons buttons)
{
    return messageBoxImplementation(
        QMessageBox::Warning, parent, title, briefText, detailedText, buttons);
}

int criticalMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText, const QMessageBox::StandardButtons buttons)
{
    return messageBoxImplementation(
        QMessageBox::Critical, parent, title, briefText, detailedText, buttons);
}

int questionMessageBox(
    QWidget * parent, const QString & title, const QString & briefText,
    const QString & detailedText, const QMessageBox::StandardButtons buttons)
{
    QMessageBox::StandardButtons actualButtons = buttons;
    actualButtons |= (QMessageBox::Ok | QMessageBox::Cancel);

    return messageBoxImplementation(
        QMessageBox::Question, parent, title, briefText, detailedText,
        actualButtons);
}

void internalErrorMessageBox(QWidget * parent, QString detailedText)
{
    if (!detailedText.isEmpty()) {
        detailedText.prepend(QObject::tr("Technical details on the issue: "));
    }

    criticalMessageBox(
        parent, QObject::tr("Internal error"),
        QObject::tr("Unfortunately, ") + QApplication::applicationName() +
            QStringLiteral(" ") +
            QObject::tr("encountered internal error. Please report "
                        "the bug to the developers and try restarting "
                        "the application"),
        detailedText);
}

} // namespace quentier
