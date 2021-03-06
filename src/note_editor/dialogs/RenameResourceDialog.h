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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DIALOGS_RENAME_RESOURCE_DIALOG_H
#define LIB_QUENTIER_NOTE_EDITOR_DIALOGS_RENAME_RESOURCE_DIALOG_H

#include <QDialog>

namespace Ui {
QT_FORWARD_DECLARE_CLASS(RenameResourceDialog)
}

namespace quentier {

class Q_DECL_HIDDEN RenameResourceDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit RenameResourceDialog(
        const QString & initialResourceName, QWidget * parent = nullptr);

    virtual ~RenameResourceDialog();

Q_SIGNALS:
    void renameAccepted(QString newResourceName);

private Q_SLOTS:
    virtual void accept() override;

private:
    Ui::RenameResourceDialog * m_pUI;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DIALOGS_RENAME_RESOURCE_DIALOG_H
