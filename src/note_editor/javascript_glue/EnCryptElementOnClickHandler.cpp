#include "EnCryptElementOnClickHandler.h"

namespace quentier {

EnCryptElementOnClickHandler::EnCryptElementOnClickHandler(QObject * parent) :
    QObject(parent)
{}

void EnCryptElementOnClickHandler::onEnCryptElementClicked(QString encryptedText, QString cipher,
                                                           QString length, QString hint, QString enCryptIndex)
{
    emit decrypt(encryptedText, cipher, length, hint, enCryptIndex, Q_NULLPTR);
}

} // namespace quentier
