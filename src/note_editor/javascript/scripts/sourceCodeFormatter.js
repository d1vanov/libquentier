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

    this.format = function()
    {
        console.log("SourceCodeFormatter::format");

        var selection = window.getSelection();
        if (!selection) {
            lastError = "selection is null";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        var anchorNode = selection.anchorNode;
        if (!anchorNode) {
            lastError = "selection anchor node is null";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        var element = anchorNode.parentNode;
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
                console.log("Found element with nodeType == 1");
                var enTag = element.getAttribute("en-tag");
                if (enTag == "en-source-code-formatted") {
                    console.log("Selection is inside source code formatted block");
                    insideSourceCodeBlock = true;
                    break;
                }
            }

            element = element.parentNode;
        }

        var selectionHtml;
        if (insideSourceCodeBlock) {
            // When already inside the source code block, need to remove the source code block formatting
            selectionHtml = element.innerHTML;
            var div = element.parentNode;
            var divParent = div.parentNode;

            observer.stop();
            try {
                divParent.removeChild(div);

                var range = document.createRange();
                range.selectNode(divParent);
                range.collapse(true);

                selection.removeAllRanges();
                selection.addRange(range);
            }
            finally {
                observer.start();
            }
        }
        else {
            selectionHtml = getSelectionHtml();
        }

        if (!selectionHtml) {
            lastError = "selection is empty";
            console.log(lastError);
            return { status:false, error:lastError };
        }

        var formattedHtml = "<div><pre style=\"font-family: Monaco, Menlo, Consolas, 'Courier New', monospace; " +
                            "font-size: 0.9em; border-radius: 4px; letter-spacing: 0.015em; padding: 1em; " +
                            "border: 1px solid #cccccc; background-color: #f8f8f8; overflow-x: auto;\" " +
                            "en-tag=en-source-code-formatted>";
        formattedHtml += selectionHtml;
        formattedHtml += "</pre></div>";

        return htmlInsertionManager.insertHtml(formattedHtml);
    }
}

(function() {
    window.sourceCodeFormatter = new SourceCodeFormatter;
})();
