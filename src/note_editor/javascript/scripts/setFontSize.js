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

function setFontSize(fontSize) {
    console.log("setFontSize: " + fontSize);

    // First check if document body is empty, if so, apply the font family to
    // the "default" body style
    body = document.body.textContent;
    body.replace(/ /g, "")
    if (body == "") {
        observer.stop();
        try {
            document.body.style.fontSize = fontSize + "pt";
        }
        finally {
            observer.start();
        }
        console.log("Applied font size to body style CSS");
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

    var anchorNode = selection.anchorNode;
    if (!anchorNode) {
        console.log("selection.anchorNode is null");
        return {
            status:false,
            appliedTo:"",
            error:"selection anchor node is null"
        };
    }

    var element = anchorNode;

    if (Object.prototype.toString.call( element ) === '[object Array]') {
        console.log("Found array of elements");
        element = element[0];
        if (!element) {
            console.log("First element of the array is null");
            return {
                status:false,
                appliedTo:"",
                error:"selection is an array which first element is null"
            };
        }
    }

    while (element.nodeType != 1) {
        element = element.parentNode;
    }

    var computedStyle = window.getComputedStyle(element);

    var scaledFontSize = parseFloat(computedStyle.fontSize) * 3.0 / 4;
    console.log("Scaled font size = " + scaledFontSize);
    var convertedFontSize = parseInt(Math.round(scaledFontSize));
    if (convertedFontSize === fontSize) {
        console.log("current font size is already equal to the new one");
        return {
            status:true,
            appliedTo:"",
            error:"current font size is already equal to new one"
        };
    }

    var selectedText = "";
    var error = "";

    observer.stop();
    try {
        if (selection.rangeCount) {
            var container = document.createElement("div");
            for (var i = 0, len = selection.rangeCount; i < len; ++i) {
                container.appendChild(selection.getRangeAt(i).cloneContents());
            }
            selectedText = container.textContent;
            container.remove();
        }

    }
    catch (e) {
        error = e.toString();
        console.warn("Exception while trying to extract selected text: " +
                     error);
    }
    finally {
        observer.start();
    }

    if (error != "") {
        return {
            status:false,
            appliedTo:"",
            error:error
        };
    }

    var html = "<span style=\"font-size:" + fontSize + "pt;\">" +
        selectedText + "</span>";
    var res = htmlInsertionManager.insertHtml(html);

    return {
        status:res.status,
        appliedTo:"selection",
        error:res.error
    };
}
