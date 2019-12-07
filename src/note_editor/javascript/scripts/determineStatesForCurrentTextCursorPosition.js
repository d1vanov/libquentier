/*
 * Copyright 2016-2019 Dmitry Ivanov
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

function determineStatesForCurrentTextCursorPosition() {
    console.log("determineStatesForCurrentTextCursorPosition");

    if (!window.hasOwnProperty('textCursorPositionHandler')) {
        console.log("textCursorPositionHandler global variable is not defined");
        return;
    }

    var selection = window.getSelection();
    if (!selection) {
        console.log("selection is null");
        return;
    }

    var node = selection.anchorNode;
    if (!node) {
        console.log("selection.anchorNode is null");
        return;
    }

    console.log("Selection anchor node: type = " + node.nodeType +
                ", name = " + node.nodeName + ", text content = " +
                node.textContent + ", anchor offset = " + selection.anchorOffset);

    var foundBold = false;
    var foundItalic = false;
    var foundUnderline = false;
    var foundStrikethrough = false;

    var foundAlignLeft = false;
    var foundAlignCenter = false;
    var foundAlignRight = false;
    var foundAlignFull = false;

    var foundOrderedList = false;
    var foundUnorderedList = false;

    var foundTable = false;

    var foundImageResource = false;
    var foundNonImageResource = false;
    var foundEnCryptTag = false;

    var textAlign;
    var firstElement = true;
    var style;

    while(node) {
        console.log("Loop iteration: node type = " + node.nodeType + ", name = " +
                    node.nodeName + ", text content = " + node.textContent);

        if (Object.prototype.toString.call(node) === '[object Array]') {
            console.log("Found array of nodes");
            node = node[0];
            if (!node) {
                console.log("First node of the array is null");
                break;
            }
        }

        while (node.nodeType != 1) {
            console.log("Going up the DOM tree to get the source node");
            node = node.parentNode;
            if (!node) {
                console.log("No further parent node");
                break;
            }
        }

        if (!node) {
            break;
        }

        if (firstElement) {
            style = window.getComputedStyle(node);
            console.log("Got style: font family = " + style.fontFamily +
                        ", font size = " + style.fontSize +
                        ", style source: " + node.outerHTML);
        }

        var enTag = node.getAttribute("en-tag");
        console.log("enTag = " + enTag + ", node name = " + node.nodeName);
        if (enTag == "en-media") {
            console.log("Found tag with en-tag = en-media");
            if (node.nodeName == "IMG") {
                foundImageResource = true;
                console.log("Found image resource: " + node.outerHTML);
                break;
            }
            else if (node.nodeName == "DIV") {
                foundNonImageResource = true;
                console.log("Found non-image resource: " + node.outerHTML);
                break;
            }
        }
        else if (enTag == "en-crypt") {
            foundEnCryptTag = true;
            console.log("Found en-crypt tag: " + node.outerHTML);
            break;
        }

        if (node.nodeName == "B") {
            foundBold = true;
            console.log("Found bold");
        }
        else if (node.nodeName == "I") {
            foundItalic = true;
            console.log("Found italic");
        }
        else if (node.nodeName == "U") {
            foundUnderline = true;
            console.log("Fount underline");
        }
        else if ((node.nodeName == "S") ||
                 (node.nodeName == "DEL") ||
                 (node.nodeName == "STRIKE")) {
            foundStrikethrough = true;
            console.log("Found strikethrough");
        }
        else if ((node.nodeName == "OL") && !foundUnorderedList) {
            foundOrderedList = true;
            console.log("Found ordered list");
        }
        else if ((node.nodeName == "UL") && !foundOrderedList) {
            foundUnorderedList = true;
            console.log("Found unordered list");
        }
        else if (node.nodeName == "TBODY") {
            foundTable = true;
            console.log("Found table");
        }

        if (!foundAlignLeft && !foundAlignCenter && !foundAlignRight && !foundAlignFull)
        {
            textAlign = node.style.textAlign;
            if (textAlign) {
                if (textAlign == "left") {
                    foundAlignLeft = true;
                    console.log("Found text align left");
                }
                else if (textAlign == "center") {
                    foundAlignCenter = true;
                    console.log("Found text align center");
                }
                else if (textAlign == "right") {
                    foundAlignRight = true;
                    console.log("Found text align right");
                }
                else if (textAlign == "justify") {
                    foundAlignFull = true;
                    console.log("Found text align justify");
                }
            }
            else {
                foundAlignLeft = true;
                console.log("Assuming text align left");
            }
        }

        node = node.parentNode;
        firstElement = false;
        console.log("Checking the next parent");
    }

    console.log("End of the loop over target elements");

    if (foundImageResource || foundNonImageResource || foundEnCryptTag) {
        console.log("foundImageResource = " + (foundImageResource ? "true" : "false") +
                    ", foundNonImageResource = " + (foundNonImageResource ? "true" : "false") +
                    ", foundEnCryptTag = " + (foundEnCryptTag ? "true" : "false"));
        return;
    }

    textCursorPositionHandler.setTextCursorPositionBoldState(foundBold);
    textCursorPositionHandler.setTextCursorPositionItalicState(foundItalic);
    textCursorPositionHandler.setTextCursorPositionUnderlineState(foundUnderline);
    textCursorPositionHandler.setTextCursorPositionStrikethroughState(foundStrikethrough);

    textCursorPositionHandler.setTextCursorPositionAlignLeftState(foundAlignLeft);
    textCursorPositionHandler.setTextCursorPositionAlignCenterState(foundAlignCenter);
    textCursorPositionHandler.setTextCursorPositionAlignRightState(foundAlignRight);
    textCursorPositionHandler.setTextCursorPositionAlignFullState(foundAlignFull);

    textCursorPositionHandler.setTextCursorPositionInsideOrderedListState(foundOrderedList);
    textCursorPositionHandler.setTextCursorPositionInsideUnorderedListState(foundUnorderedList);

    textCursorPositionHandler.setTextCursorPositionInsideTableState(foundTable);

    if (style) {
        var scaledFontSize = parseFloat(style.fontSize) * 3.0 / 4;
        var convertedFontSize = parseInt(Math.round(scaledFontSize));
        console.log("Notifying of font params change: font family = " + style.fontFamily +
                    ", font size = " + style.fontSize + ", converted font size in pt = " +
                    convertedFontSize);
        textCursorPositionHandler.setTextCursorPositionFontName(style.fontFamily);
        textCursorPositionHandler.setTextCursorPositionFontSize(convertedFontSize);
    }
    else {
        console.log("Computed style is null");
    }
}
