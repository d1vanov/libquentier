/*
 * Copyright 2017 Dmitry Ivanov
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

function SourceCodeFormatter() {
    var lastError;
    var formatStyle = "font-family: Monaco, Menlo, Consolas, 'Courier New', monospace; " +
                      "font-size: 0.9em; border-radius: 4px; letter-spacing: 0.015em; padding: 1em; " +
                      "border: 1px solid #cccccc; background-color: #f8f8f8; overflow-x: auto;"

    this.format = function()
    {
        console.log("SourceCodeFormatter::format");

        var selection = window.getSelection();
        if (!selection) {
            lastError = "selection is null";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        var rangeCount = selection.rangeCount;
        if (rangeCount != 1) {
            lastError = "not exactly one range within the selection";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var anchorNode = selection.anchorNode;
        if (!anchorNode) {
            lastError = "selection anchor node is null";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        var element = anchorNode;
        var insideSourceCodeBlock = false;

        while(element) {
            if (Object.prototype.toString.call( element ) === '[object Array]') {
                console.log("Found array of elements");
                element = element[0];
                if (!element) {
                    console.log("First element of the array is null");
                    break;
                }
            }

            if (element.nodeType == 1) {
                if (element.nodeName == "PRE") {
                    var elementStyle = element.getAttribute("style");
                    if (elementStyle == formatStyle) {
                        console.log("Selection is inside source code formatted block");
                        insideSourceCodeBlock = true;
                        break;
                    }
                }
            }

            element = element.parentNode;
        }

        if (insideSourceCodeBlock) {
            // When already inside the source code block, need to remove the source code block formatting
            var containedHtml = element.innerHTML;
            console.log("Contained HTML: " + containedHtml);

            var div = element.parentNode;
            var divParent = div.parentNode;

            var newSelectionNode = div.previousSibling;
            while(newSelectionNode) {
                if (newSelectionNode.nodeType != 1) {
                    newSelectionNode = newSelectionNode.previousSibling;
                }
                else {
                    break;
                }
            }

            if (!newSelectionNode) {
                newSelectionNode = div.parentNode;
            }

            observer.stop();
            try {
                divParent.removeChild(div);

                var range = document.createRange();
                range.selectNode(newSelectionNode);
                range.collapse();

                selection.removeAllRanges();
                selection.addRange(range);
            }
            finally {
                observer.start();
            }

            var res = htmlInsertionManager.insertHtml(containedHtml);
            console.log("After htmlInsertionManager::insertHtml: " + document.body.innerHTML);
            return res;
        }

        var selectionHtml = getSelectionHtml();
        if (!selectionHtml) {
            lastError = "selection is empty";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        console.log("Selection html: " + selectionHtml);

        var formattedHtml = "<div><pre style=\"" + formatStyle + "\">";
        formattedHtml += selectionHtml;
        formattedHtml += "</pre></div>";

        var res = htmlInsertionManager.insertHtml(formattedHtml);
        console.log("After htmlInsertionManager::insertHtml: " + document.body.innerHTML);
        return res;
    }
}

(function() {
    window.sourceCodeFormatter = new SourceCodeFormatter;
})();
