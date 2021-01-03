/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "TextCursorPositionJavaScriptHandler.h"

namespace quentier {

TextCursorPositionJavaScriptHandler::TextCursorPositionJavaScriptHandler(
    QObject * parent) :
    QObject(parent)
{}

void TextCursorPositionJavaScriptHandler::onTextCursorPositionChange()
{
    Q_EMIT textCursorPositionChanged();
}

void TextCursorPositionJavaScriptHandler::setOnImageResourceState(
    const bool state, const QString resourceHash)
{
    Q_EMIT textCursorPositionOnImageResourceState(
        state, QByteArray::fromHex(resourceHash.toLocal8Bit()));
}

void TextCursorPositionJavaScriptHandler::setOnNonImageResourceState(
    const bool state, const QString resourceHash)
{
    Q_EMIT textCursorPositionOnNonImageResourceState(
        state, QByteArray::fromHex(resourceHash.toLocal8Bit()));
}

void TextCursorPositionJavaScriptHandler::setOnEnCryptTagState(
    const bool state, const QString encryptedText, const QString cipher,
    const QString length)
{
    Q_EMIT textCursorPositionOnEnCryptTagState(
        state, encryptedText, cipher, length);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionBoldState(
    const bool bold)
{
    Q_EMIT textCursorPositionBoldState(bold);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionItalicState(
    const bool italic)
{
    Q_EMIT textCursorPositionItalicState(italic);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionUnderlineState(
    const bool underline)
{
    Q_EMIT textCursorPositionUnderlineState(underline);
}

void TextCursorPositionJavaScriptHandler::
    setTextCursorPositionStrikethroughState(const bool strikethrough)
{
    Q_EMIT textCursorPositionStrikethroughState(strikethrough);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionAlignLeftState(
    const bool state)
{
    Q_EMIT textCursorPositionAlignLeftState(state);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionAlignCenterState(
    const bool state)
{
    Q_EMIT textCursorPositionAlignCenterState(state);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionAlignRightState(
    const bool state)
{
    Q_EMIT textCursorPositionAlignRightState(state);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionAlignFullState(
    const bool state)
{
    Q_EMIT textCursorPositionAlignFullState(state);
}

void TextCursorPositionJavaScriptHandler::
    setTextCursorPositionInsideOrderedListState(const bool insideOrderedList)
{
    Q_EMIT textCursorPositionInsideOrderedListState(insideOrderedList);
}

void TextCursorPositionJavaScriptHandler::
    setTextCursorPositionInsideUnorderedListState(
        const bool insideUnorderedList)
{
    Q_EMIT textCursorPositionInsideUnorderedListState(insideUnorderedList);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionInsideTableState(
    const bool insideTable)
{
    Q_EMIT textCursorPositionInsideTableState(insideTable);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionFontName(
    const QString fontSize)
{
    Q_EMIT textCursorPositionFontName(fontSize);
}

void TextCursorPositionJavaScriptHandler::setTextCursorPositionFontSize(
    const int fontSize)
{
    Q_EMIT textCursorPositionFontSize(fontSize);
}

} // namespace quentier
