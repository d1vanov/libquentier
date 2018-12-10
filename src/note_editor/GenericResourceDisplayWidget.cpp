/*
 * Copyright 2016 Dmitry Ivanov
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
#include "NoteEditorSettingsName.h"
#include "ResourceDataInTemporaryFileStorageManager.h"
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/Utility.h>
#include <quentier/utility/MessageBox.h>
#include <quentier/types/Resource.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMessageBox>
#include <QCryptographicHash>

namespace quentier {

GenericResourceDisplayWidget::GenericResourceDisplayWidget(QWidget * parent) :
    QWidget(parent),
    m_pUI(new Ui::GenericResourceDisplayWidget),
    m_resourceLocalUid(),
    m_resourceHash()
{
    m_pUI->setupUi(this);
}

GenericResourceDisplayWidget::~GenericResourceDisplayWidget()
{
    delete m_pUI;
}

void GenericResourceDisplayWidget::initialize(const QIcon & icon, const QString & name,
                                              const QString & size, const Resource & resource)
{
    QNDEBUG(QStringLiteral("GenericResourceDisplayWidget::initialize: name = ") << name << QStringLiteral(", size = ") << size);

    m_resourceLocalUid = resource.localUid();

    if (resource.hasDataHash()) {
        m_resourceHash = resource.dataHash();
    }
    else if (resource.hasDataBody()) {
        m_resourceHash = QCryptographicHash::hash(resource.dataBody(), QCryptographicHash::Md5);
    }
    else if (resource.hasAlternateDataHash()) {
        m_resourceHash = resource.alternateDataHash();
    }
    else if (resource.hasAlternateDataBody()) {
        m_resourceHash = QCryptographicHash::hash(resource.alternateDataBody(), QCryptographicHash::Md5);
    }

    updateResourceName(name);
    m_pUI->resourceDisplayNameLabel->setTextFormat(Qt::RichText);

    updateResourceSize(size);
    m_pUI->resourceSizeLabel->setTextFormat(Qt::RichText);

    m_pUI->resourceIconLabel->setPixmap(icon.pixmap(QSize(16,16)));

    if (!QIcon::hasThemeIcon(QStringLiteral("document-open"))) {
        m_pUI->openResourceButton->setIcon(QIcon(QStringLiteral(":/generic_resource_icons/png/open_with.png")));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("document-save-as"))) {
        m_pUI->saveResourceButton->setIcon(QIcon(QStringLiteral(":/generic_resource_icons/png/save.png")));
    }

    QObject::connect(m_pUI->openResourceButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(GenericResourceDisplayWidget,onOpenResourceInExternalAppButtonPressed));
    QObject::connect(m_pUI->saveResourceButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(GenericResourceDisplayWidget,onSaveResourceDataToFileButtonPressed));
}

QString GenericResourceDisplayWidget::resourceLocalUid() const
{
    return m_resourceLocalUid;
}

void GenericResourceDisplayWidget::updateResourceName(const QString & resourceName)
{
    m_pUI->resourceDisplayNameLabel->setText(QStringLiteral("<html><head/><body><p><span style=\" font-size:8pt;\">") +
                                             resourceName + QStringLiteral("</span></p></body></head></html>"));
}

void GenericResourceDisplayWidget::updateResourceSize(const QString & size)
{
    m_pUI->resourceSizeLabel->setText(QStringLiteral("<html><head/><body><p><span style=\" font-size:8pt;\">") + size +
                                      QStringLiteral("</span></p></body></head></html>"));
}

void GenericResourceDisplayWidget::onOpenResourceInExternalAppButtonPressed()
{
    QNDEBUG(QStringLiteral("GenericResourceDisplayWidget::onOpenResourceInExternalAppButtonPressed"));

    if (m_resourceHash.isEmpty()) {
        QNDEBUG(QStringLiteral("Can't open resource: resource hash is empty"));
        return;
    }

    Q_EMIT openResourceRequest(m_resourceHash);
}

void GenericResourceDisplayWidget::onSaveResourceDataToFileButtonPressed()
{
    QNDEBUG(QStringLiteral("GenericResourceDisplayWidget::onSaveResourceDataToFileButtonPressed"));

    if (m_resourceHash.isEmpty()) {
        QNDEBUG(QStringLiteral("Can't save resource: resource hash is empty"));
        return;
    }

    Q_EMIT saveResourceRequest(m_resourceHash);
}

} // namespace quentier
