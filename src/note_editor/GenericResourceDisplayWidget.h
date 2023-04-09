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

#ifndef LIB_QUENTIER_NOTE_EDITOR_GENERIC_RESOURCE_DISPLAY_WIDGET_H
#define LIB_QUENTIER_NOTE_EDITOR_GENERIC_RESOURCE_DISPLAY_WIDGET_H

#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>

#include <QUuid>
#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QMimeDatabase)

namespace Ui {
QT_FORWARD_DECLARE_CLASS(GenericResourceDisplayWidget)
}

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Resource)

class Q_DECL_HIDDEN GenericResourceDisplayWidget final : public QWidget
{
    Q_OBJECT
public:
    GenericResourceDisplayWidget(QWidget * parent = nullptr);
    virtual ~GenericResourceDisplayWidget();

    void initialize(
        const QIcon & icon, const QString & name, const QString & size,
        const Resource & resource);

    QString resourceLocalUid() const;

    void updateResourceName(const QString & resourceName);
    void updateResourceSize(const QString & size);

Q_SIGNALS:
    void openResourceRequest(const QByteArray & resourceHash);
    void saveResourceRequest(const QByteArray & resourceHash);

private Q_SLOTS:
    void onOpenResourceInExternalAppButtonPressed();
    void onSaveResourceDataToFileButtonPressed();

private:
    Q_DISABLE_COPY(GenericResourceDisplayWidget)

private:
    Ui::GenericResourceDisplayWidget * m_pUi;
    QString m_resourceLocalUid;
    QByteArray m_resourceHash;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_GENERIC_RESOURCE_DISPLAY_WIDGET_H
