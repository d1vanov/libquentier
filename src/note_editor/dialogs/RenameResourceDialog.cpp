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

#include "RenameResourceDialog.h"
#include "ui_RenameResourceDialog.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

RenameResourceDialog::RenameResourceDialog(
    const QString & initialResourceName, QWidget * parent) :
    QDialog(parent),
    m_pUI(new Ui::RenameResourceDialog)
{
    m_pUI->setupUi(this);
    m_pUI->lineEdit->setText(initialResourceName);
}

RenameResourceDialog::~RenameResourceDialog()
{
    delete m_pUI;
}

void RenameResourceDialog::accept()
{
    QNDEBUG("note_editor:dialog", "RenameResourceDialog::accept");
    Q_EMIT renameAccepted(m_pUI->lineEdit->text());
    QDialog::accept();
}

} // namespace quentier
