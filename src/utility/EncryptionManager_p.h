#ifndef LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_P_H
#define LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_P_H

#include <quentier/utility/QNLocalizedString.h>
#include <QVector>

// Evernote service defined constants
#define EN_ITERATIONS (50000)
#define EN_AES_KEYSIZE (16)
#define EN_RC2_KEYSIZE (8)
#define EN_AES_HMACSIZE (32)
#define EN_RC2_HMACSIZE (16)
#define EN_IDENT "ENC0"
#define MAX_PADDING_LEN (16)

namespace quentier {

class EncryptionManagerPrivate
{
public:
    EncryptionManagerPrivate();
    ~EncryptionManagerPrivate();

    bool decrypt(const QString & encryptedText, const QString & passphrase,
                 const QString & cipher, const size_t keyLength,
                 QString & decryptedText, QNLocalizedString & errorDescription);

    bool encrypt(const QString & textToEncrypt, const QString & passphrase,
                 QString & cipher, size_t & keyLength,
                 QString & encryptedText, QNLocalizedString & errorDescription);

private:
    // AES encryption/decryption routines
    struct SaltKind
    {
        enum type {
            SALT = 0,
            SALTMAC,
            IV
        };
    };

    bool generateSalt(const SaltKind::type saltKind, const size_t saltSize, QNLocalizedString & errorDescription);

    bool generateKey(const QByteArray & passphraseData, const unsigned char * salt,
                     const size_t keySize, QNLocalizedString & errorDescription);

    bool calculateHmac(const QByteArray & passphraseData, const unsigned char * salt,
                       const QByteArray & encryptedTextData, const size_t keySize,
                       QNLocalizedString & errorDescription);

    bool encyptWithAes(const QByteArray & textToEncrypt, QByteArray & encryptedText,
                       QNLocalizedString & errorDescription);

    bool decryptAes(const QString & encryptedText, const QString & passphrase,
                    QByteArray & decryptedText, QNLocalizedString & errorDescription);

    bool splitEncryptedData(const QString & encryptedData, const size_t saltSize,
                            const size_t hmacSize, QByteArray & encryptedText,
                            QNLocalizedString & errorDescription);

private:
    // RC2 decryption routines
    bool decryptRc2(const QString & encryptedText, const QString & passphrase,
                    QString & decryptedText, QNLocalizedString & errorDescription);

    void rc2KeyCodesFromPassphrase(const QString & passphrase) const;
    QString decryptRc2Chunk(const QByteArray & inputCharCodes, const QVector<int> & key) const;

    qint32 crc32(const QString & str) const;

private:
    unsigned char m_salt[EN_AES_KEYSIZE];
    unsigned char m_saltmac[EN_AES_KEYSIZE];
    unsigned char m_iv[EN_AES_KEYSIZE];

    unsigned char m_key[EN_AES_KEYSIZE];
    unsigned char m_hmac[EN_AES_HMACSIZE];

    // Cache helpers
    mutable QVector<int>        m_cached_xkey;
    mutable QVector<int>        m_cached_key;
    mutable int                 m_decrypt_rc2_chunk_key_codes[8];
    mutable QString             m_rc2_chunk_out;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_ENCRYPTION_MANAGER_P_H
