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

#include "GenericResourceDisplayWidget.h"
#include "ui_GenericResourceDisplayWidget.h"

#include "NoteEditorSettingsNames.h"
#include "ResourceDataInTemporaryFileStorageManager.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/MessageBox.h>
#include <quentier/utility/QuentierCheckPtr.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace quentier {

GenericResourceDisplayWidget::GenericResourceDisplayWidget(QWidget * parent) :
    QWidget(parent), m_pUi(new Ui::GenericResourceDisplayWidget),
    m_resourceLocalUid(), m_resourceHash()
{
    m_pUi->setupUi(this);
}

GenericResourceDisplayWidget::~GenericResourceDisplayWidget()
{
    delete m_pUi;
}

void GenericResourceDisplayWidget::initialize(
    const QIcon & icon, const QString & name, const QString & size,
    const Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "GenericResourceDisplayWidget::initialize: name = "
            << name << ", size = " << size);

    m_resourceLocalUid = resource.localUid();

    if (resource.hasDataHash()) {
        m_resourceHash = resource.dataHash();
    }
    else if (resource.hasDataBody()) {
        m_resourceHash = QCryptographicHash::hash(
            resource.dataBody(), QCryptographicHash::Md5);
    }
    else if (resource.hasAlternateDataHash()) {
        m_resourceHash = resource.alternateDataHash();
    }
    else if (resource.hasAlternateDataBody()) {
        m_resourceHash = QCryptographicHash::hash(
            resource.alternateDataBody(), QCryptographicHash::Md5);
    }

    updateResourceName(name);
    m_pUi->resourceDisplayNameLabel->setTextFormat(Qt::RichText);

    updateResourceSize(size);
    m_pUi->resourceSizeLabel->setTextFormat(Qt::RichText);

    m_pUi->resourceIconLabel->setPixmap(icon.pixmap(QSize(16, 16)));

    if (!QIcon::hasThemeIcon(QStringLiteral("document-open"))) {
        m_pUi->openResourceButton->setIcon(QIcon(
            QStringLiteral(":/generic_resource_icons/png/open_with.png")));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("document-save-as"))) {
        m_pUi->saveResourceButton->setIcon(
            QIcon(QStringLiteral(":/generic_resource_icons/png/save.png")));
    }

    QObject::connect(
        m_pUi->openResourceButton, &QPushButton::released, this,
        &GenericResourceDisplayWidget::
            onOpenResourceInExternalAppButtonPressed);

    QObject::connect(
        m_pUi->saveResourceButton, &QPushButton::released, this,
        &GenericResourceDisplayWidget::onSaveResourceDataToFileButtonPressed);
}

QString GenericResourceDisplayWidget::resourceLocalUid() const
{
    return m_resourceLocalUid;
}

void GenericResourceDisplayWidget::updateResourceName(
    const QString & resourceName)
{
    m_pUi->resourceDisplayNameLabel->setText(
        QStringLiteral(
            "<html><head/><body><p><span style=\" font-size:8pt;\">") +
        resourceName + QStringLiteral("</span></p></body></head></html>"));
}

void GenericResourceDisplayWidget::updateResourceSize(const QString & size)
{
    m_pUi->resourceSizeLabel->setText(
        QStringLiteral(
            "<html><head/><body><p><span style=\" font-size:8pt;\">") +
        size + QStringLiteral("</span></p></body></head></html>"));
}

void GenericResourceDisplayWidget::onOpenResourceInExternalAppButtonPressed()
{
    QNDEBUG(
        "note_editor",
        "GenericResourceDisplayWidget::"
            << "onOpenResourceInExternalAppButtonPressed");

    if (m_resourceHash.isEmpty()) {
        QNDEBUG("note_editor", "Can't open resource: resource hash is empty");
        return;
    }

    Q_EMIT openResourceRequest(m_resourceHash);
}

void GenericResourceDisplayWidget::onSaveResourceDataToFileButtonPressed()
{
    QNDEBUG(
        "note_editor",
        "GenericResourceDisplayWidget::"
            << "onSaveResourceDataToFileButtonPressed");

    if (m_resourceHash.isEmpty()) {
        QNDEBUG("note_editor", "Can't save resource: resource hash is empty");
        return;
    }

    Q_EMIT saveResourceRequest(m_resourceHash);
}

} // namespace quentier
