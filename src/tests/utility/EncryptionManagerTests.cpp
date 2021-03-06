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

#include "EncryptionManagerTests.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/EncryptionManager.h>

namespace quentier {
namespace test {

bool decryptAesTest(QString & error)
{
    EncryptionManager manager;

    const QString encryptedText = QStringLiteral(
        "RU5DMI1mnQ7fKjBk9f0a57gSc9Nfbuw3uuwMKs32Y+wJGLZa0N8PcTzf7pu3"
        "/2VOBqZMvfkKGh4mnJuGy45ZT2TwOfqt+ey8Tic7BmhGg7b4n+SpJFHntkeL"
        "glxFWJt6oIG14i7IpamIuYyE5XcBRkOQs2cr7rg730d1hxx6sW/KqIfdr+0rF4k"
        "+rqP7tpI5ha/ALkhaZAuDbIVic39aCRcu6uve6mHHHPA03olCbi7ePVwO7e94mp"
        "uvcg2lGTJyDw/NoZmjFycjXESRJgLIr+gGfyD17jYNGcPBLR8Rb0M9vGK1tG9haG"
        "+Vem1pTWgRfYXF70mMduEmAd4xXy1JqV6XNUYDddW9iPpffWTZgD409LK9wIZM5C"
        "W2rbM2lwM/R0IEnoK7N5X8lCOzqkA9H/HF+8E=");

    const QString passphrase =
        QStringLiteral("thisismyriflethisismygunthisisforfortunethisisforfun");

    const QString originalText = QStringLiteral(
        "<span style=\"display: inline !important; float: none; \">"
        "Ok, here's some really long text. I can type and type it "
        "on and on and it will not stop any time soon just yet. "
        "The password is going to be long also.&nbsp;</span>");

    QString decryptedText;
    ErrorString errorMessage;

    bool res = manager.decrypt(
        encryptedText, passphrase, QStringLiteral("AES"), 128, decryptedText,
        errorMessage);

    if (!res) {
        error = errorMessage.nonLocalizedString();
        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    if (decryptedText != originalText) {
        error = QStringLiteral("Decrypted text differs from the original; ") +
            QStringLiteral("original text = ") + originalText +
            QStringLiteral("; decrypted text = ") + decryptedText;

        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    return true;
}

bool encryptDecryptTest(QString & error)
{
    EncryptionManager manager;

    const QString textToEncrypt = QStringLiteral("Very-very secret");
    const QString passphrase = QStringLiteral("rough_awakening^");

    ErrorString errorMessage;
    QString encryptedText;
    QString cipher;
    size_t keyLength = 0;

    bool res = manager.encrypt(
        textToEncrypt, passphrase, cipher, keyLength, encryptedText,
        errorMessage);

    if (!res) {
        error = errorMessage.nonLocalizedString();
        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    errorMessage.clear();
    QString decryptedText;

    res = manager.decrypt(
        encryptedText, passphrase, cipher, keyLength, decryptedText,
        errorMessage);

    if (!res) {
        error = errorMessage.nonLocalizedString();
        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    if (decryptedText != QStringLiteral("Very-very secret")) {
        error = QStringLiteral("Decrypted text differs from the original ") +
            QStringLiteral("(\"Very-very secret\"): ") + decryptedText;

        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    return true;
}

bool decryptRc2Test(QString & error)
{
    EncryptionManager manager;

    const QString encryptedText = QStringLiteral(
        "K+sUXSxI2Mt075+pSDxR/gnCNIEnk5XH1P/D0Eie17"
        "JIWgGnNo5QeMo3L0OeBORARGvVtBlmJx6vJY2Ij/2En"
        "MVy6/aifSdZXAxRlfnTLvI1IpVgHpTMzEfy6zBVMo+V"
        "Bt2KglA+7L0iSjA0hs3GEHI6ZgzhGfGj");

    const QString passphrase = QStringLiteral("my_own_encryption_key_1988");

    const QString originalText = QStringLiteral(
        "<span style=\"display: inline !important; float: none; \">"
        "Ok, here's a piece of text I'm going to encrypt now</span>");

    ErrorString errorMessage;
    QString decryptedText;

    bool res = manager.decrypt(
        encryptedText, passphrase, QStringLiteral("RC2"), 64, decryptedText,
        errorMessage);

    if (!res) {
        error = errorMessage.nonLocalizedString();
        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    if (decryptedText != originalText) {
        error = QStringLiteral("Decrypted text differs from the original; ") +
            QStringLiteral("original text = ") + originalText +
            QStringLiteral("; decrypted text = ") + decryptedText;

        QNWARNING("tests:utility_encryption", error);
        return false;
    }

    return true;
}

} // namespace test
} // namespace quentier
