/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/utility/Factory.h>
#include <quentier/utility/IEncryptor.h>

#include <QTextStream>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::utility::tests {

namespace {

void checkDecryptedText(
    const QString & decryptedText, const QString & originalText)
{
    const auto composeErrorText = [&] {
        QString errorText;
        QTextStream strm{&errorText};

        strm << "Decrypted text differs from the original; original text = "
             << originalText << "\n\nDecrypted text = " << decryptedText
             << "\n\n";

        const QByteArray originalTextUtf8 = originalText.toUtf8();
        const QByteArray decryptedTextUtf8 = decryptedText.toUtf8();

        if (originalTextUtf8.size() != decryptedTextUtf8.size()) {
            strm << "Sizes of original text and decrypted text in UTF-8 don't "
                 << "match: " << originalTextUtf8.size() << " vs "
                 << decryptedTextUtf8.size()
                 << "\n\nOriginal text characters:\n";

            int counter = 0;
            for (const char c: originalTextUtf8) {
                strm << "   [" << counter << ": " << QChar::fromLatin1(c)
                     << " (" << static_cast<int>(c) << ")];\n";
                ++counter;
            }

            strm << "\n\nDecrypted text characters:\n";
            counter = 0;
            for (const char c: decryptedTextUtf8) {
                strm << "   [" << counter << ": " << QChar::fromLatin1(c)
                     << " (" << static_cast<int>(c) << ")];\n";
                ++counter;
            }
        }
        else {
            for (decltype(originalTextUtf8.size()) i = 0;
                 i < originalTextUtf8.size(); ++i)
            {
                const auto orig = originalTextUtf8.at(i);
                const auto decr = decryptedTextUtf8.at(i);
                if (orig != decr) {
                    strm << "Found diff in bytes at position " << i
                         << "original character: " << QChar::fromLatin1(orig)
                         << " (" << static_cast<int>(orig)
                         << "), decrypted text character: "
                         << QChar::fromLatin1(decr) << " ("
                         << static_cast<int>(decr) << ")\n";
                }
            }

            strm << "\n\nOriginal vs decrypted text characters:\n";
            int counter = 0;
            for (decltype(originalTextUtf8.size()) i = 0;
                 i < originalTextUtf8.size(); ++i)
            {
                const auto orig = originalTextUtf8.at(i);
                const auto decr = decryptedTextUtf8.at(i);

                strm << "   [" << counter << ": " << QChar::fromLatin1(orig)
                     << " (" << static_cast<int>(orig) << ") vs "
                     << QChar::fromLatin1(decr) << " ("
                     << static_cast<int>(decr) << ")];\n";
                ++counter;
            }
        }

        strm.flush();
        return errorText;
    };

    EXPECT_EQ(decryptedText.localeAwareCompare(originalText), 0)
        << composeErrorText().toStdString();
}

} // namespace

TEST(EncryptorTest, DecryptAES)
{
    auto encryptor = createOpenSslEncryptor();

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

    const auto res =
        encryptor->decrypt(encryptedText, passphrase, IEncryptor::Cipher::AES);
    ASSERT_TRUE(res.isValid())
        << res.error().nonLocalizedString().toStdString();

    checkDecryptedText(res.get(), originalText);
}

TEST(EncryptorTest, EncryptAndDecryptAES)
{
    auto encryptor = createOpenSslEncryptor();

    const QString textToEncrypt = QStringLiteral("Very-very secret");
    const QString passphrase = QStringLiteral("rough_awakening^");

    auto res = encryptor->encrypt(textToEncrypt, passphrase);
    ASSERT_TRUE(res.isValid())
        << res.error().nonLocalizedString().toStdString();

    QString encryptedText = res.get();

    res =
        encryptor->decrypt(encryptedText, passphrase, IEncryptor::Cipher::AES);
    ASSERT_TRUE(res.isValid())
        << res.error().nonLocalizedString().toStdString();

    checkDecryptedText(res.get(), textToEncrypt);
}

TEST(EncryptorTest, decryptRC2)
{
    auto encryptor = createOpenSslEncryptor();

    const QString encryptedText = QStringLiteral(
        "K+sUXSxI2Mt075+pSDxR/gnCNIEnk5XH1P/D0Eie17"
        "JIWgGnNo5QeMo3L0OeBORARGvVtBlmJx6vJY2Ij/2En"
        "MVy6/aifSdZXAxRlfnTLvI1IpVgHpTMzEfy6zBVMo+V"
        "Bt2KglA+7L0iSjA0hs3GEHI6ZgzhGfGj");

    const QString passphrase = QStringLiteral("my_own_encryption_key_1988");

    const QString originalText = QStringLiteral(
        "<span style=\"display: inline !important; float: none; \">"
        "Ok, here's a piece of text I'm going to encrypt now</span>");

    const auto res =
        encryptor->decrypt(encryptedText, passphrase, IEncryptor::Cipher::RC2);
    ASSERT_TRUE(res.isValid())
        << res.error().nonLocalizedString().toStdString();

    checkDecryptedText(res.get(), originalText);
}

} // namespace quentier::utility::tests
