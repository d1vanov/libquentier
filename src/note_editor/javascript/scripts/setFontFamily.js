/*
 * Copyright 2019 Dmitry Ivanov
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

function setFontFamily(fontFamily) {
    console.log("setFontFamily: " + fontFamily);

    // First check if document body is empty, if so, apply the font family to
    // the "default" body style
    body = document.body.innerHTML;
    body.replace(/ /g, "")
    if (body == "") {
        observer.stop();
        try {
            document.body.style.fontFamily = fontFamily;
        }
        finally {
            observer.start();
        }
        console.log("Applied font family to body style CSS");
        return {
            status:true,
            appliedTo:"bodyStyle",
            error:""
        };
    }

    var selection = window.getSelection();
    if (!selection) {
        console.log("selection is null");
        return {
            status:false,
            appliedTo:"",
            error:"selection is null"
        };
    }

    if (!selection.rangeCount) {
        console.log("selection range count is 0");
        return {
            status:false,
            appliedTo:"",
            error:"selection range count is 0"
        };
    }

    if (selection.rangeCount > 1 || !selection.getRangeAt(0).collapsed) {
        // FIXME: apparently this doesn't really work - wrapping existing span
        // with another span with different style doesn't really change the style
        // of the displayed text
        html = "<span style=\"font-family:&quot;" + fontFamily + "&quot;\">" +
            getSelectionHtml() + "</span>";
        document.execCommand("insertHTML", false, html);
        return {
            status:true,
            appliedTo:"selection",
            error:""
        };
    }

    var anchorNode = selection.anchorNode;
    if (!anchorNode) {
        console.log("selection.anchorNode is null");
        return {
            status:false,
            appliedTo:"",
            error:"selection.anchorNode is null"
        }
    }

    console.log("Anchor node type is " + anchorNode.nodeType);

    if (anchorNode.nodeType != 1 || anchorNode.nodeName != "SPAN") {
        var newSpan = document.createElement("SPAN");
        newSpan.style.fontFamily = fontFamily;
        if (anchorNode.nodeType != 1) {
            anchorNode.parentNode.insertBefore(newSpan, anchorNode.nextSibling);
        }
        else {
            anchorNode.appendChild(newSpan);
        }

        var range = document.createRange();
        range.selectNodeContents(newSpan);
        range.collapse(false);
        selection.removeAllRanges();
        selection.addRange(range);

        return {
            status:true,
            appliedTo:"selection",
            error:""
        };
    }

    console.log("Selection's anchor node is at span element, changing its face attribute");
    anchorNode.style.fontFamily = fontFamily;
    return {
        status:true,
        appliedTo:"span node",
        error:""
    };
}
