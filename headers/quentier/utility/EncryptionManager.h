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

#ifndef LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_H
#define LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_H

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QObject>
#include <QString>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(EncryptionManagerPrivate)

/**
 * @brief The EncryptionManager class provides both synchronous methods to
 * encrypt or decrypt given text with password, cipher and key length and their
 * signal-slot based potentially asynchronous counterparts
 */
class QUENTIER_EXPORT EncryptionManager : public QObject
{
    Q_OBJECT
public:
    explicit EncryptionManager(QObject * parent = nullptr);
    virtual ~EncryptionManager();

    bool decrypt(
        const QString & encryptedText, const QString & passphrase,
        const QString & cipher, const size_t keyLength, QString & decryptedText,
        ErrorString & errorDescription);

    bool encrypt(
        const QString & textToEncrypt, const QString & passphrase,
        QString & cipher, size_t & keyLength, QString & encryptedText,
        ErrorString & errorDescription);

Q_SIGNALS:
    void decryptedText(
        QString text, bool success, ErrorString errorDescription,
        QUuid requestId);

    void encryptedText(
        QString encryptedText, bool success, ErrorString errorDescription,
        QUuid requestId);

public Q_SLOTS:
    void onDecryptTextRequest(
        QString encryptedText, QString passphrase, QString cipher,
        size_t keyLength, QUuid requestId);

    void onEncryptTextRequest(
        QString textToEncrypt, QString passphrase, QString cipher,
        size_t keyLength, QUuid requestId);

private:
    EncryptionManagerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(EncryptionManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_H
