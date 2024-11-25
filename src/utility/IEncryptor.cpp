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

#include <quentier/utility/IEncryptor.h>

#include <QDebug>
#include <QTextStream>

namespace quentier {

namespace {

template <class T>
void printCipher(const IEncryptor::Cipher cipher, T & t)
{
    switch (cipher)
    {
    case IEncryptor::Cipher::AES:
        t << "AES";
        break;
    case IEncryptor::Cipher::RC2:
        t << "RC2";
        break;
    }
}

} // namespace

IEncryptor::~IEncryptor() noexcept = default;

QDebug & operator<<(QDebug & dbg, const IEncryptor::Cipher cipher)
{
    printCipher(cipher, dbg);
    return dbg;
}

QTextStream & operator<<(QTextStream & strm, const IEncryptor::Cipher cipher)
{
    printCipher(cipher, strm);
    return strm;
}

} // namespace quentier
