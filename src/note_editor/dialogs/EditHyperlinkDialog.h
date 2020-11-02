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

#ifndef LIB_QUENTIER_NOTE_EDITOR_EDIT_HYPERLINK_DIALOG_H
#define LIB_QUENTIER_NOTE_EDITOR_EDIT_HYPERLINK_DIALOG_H

#include <QDialog>
#include <QUrl>

namespace Ui {
QT_FORWARD_DECLARE_CLASS(EditHyperlinkDialog)
}

namespace quentier {

class Q_DECL_HIDDEN EditHyperlinkDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit EditHyperlinkDialog(
        QWidget * parent = nullptr, const QString & startupText = {},
        const QString & startupUrl = {}, const quint64 idNumber = 0);

    virtual ~EditHyperlinkDialog();

Q_SIGNALS:
    void editHyperlinkAccepted(
        QString text, QUrl url, quint64 idNumber, bool startupUrlWasEmpty);

private Q_SLOTS:
    virtual void accept() override;

    void onUrlEdited(QString url);
    void onUrlEditingFinished();

private:
    bool validateAndGetUrl(QUrl & url);

private:
    Ui::EditHyperlinkDialog * m_pUI;
    const quint64 m_idNumber;
    const bool m_startupUrlWasEmpty;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_EDIT_HYPERLINK_DIALOG_H
