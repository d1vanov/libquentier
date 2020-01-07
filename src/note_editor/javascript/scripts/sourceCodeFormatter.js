/*
 * Copyright 2017-2019 Dmitry Ivanov
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
    NodeUndoRedoManager.call(this);

    var formatStyle = "font-family: Monaco, Menlo, Consolas, 'Courier New', monospace; " +
                      "font-size: 0.9em; border-radius: 4px; letter-spacing: 0.015em; padding: 1em; " +
                      "border: 1px solid #cccccc; background-color: #f8f8f8; color: black; overflow-x: auto;";
    var lastError;
    var lastFeedback;

    this.format = function()
    {
        console.log("SourceCodeFormatter::format");

        var selection = window.getSelection();
        if (!selection) {
            lastError = "selection is null";
            lastFeedback = "";
            console.log(lastError);
            return { status:false, error:lastError, feedback:lastFeedback };
        }

        var rangeCount = selection.rangeCount;
        if (rangeCount != 1) {
            lastError = "";
            lastFeedback = "not exactly one range within the selection";
            console.log(lastFeedback);
            return { status:false, error:lastError, feedback:lastFeedback };
        }

        var anchorNode = selection.anchorNode;
        if (!anchorNode) {
            lastError = "";
            lastFeedback = "no anchor node within the selection";
            console.log(lastFeedback);
            return { status:false, error:lastError, feedback:lastFeedback };
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

            if ((element.nodeType == 1) && (element.nodeName == "PRE")) {
                var elementStyle = element.getAttribute("style");
                if (elementStyle == formatStyle) {
                    console.log("Selection is inside source code formatted block");
                    insideSourceCodeBlock = true;
                    break;
                }
            }

            element = element.parentNode;
        }

        if (insideSourceCodeBlock) {
            return this.unwrap(element);
        }

        var selectedText = selection.toString();
        if (!selectedText) {
            lastError = "";
            lastFeedback = "Selected text is empty";
            console.log(lastFeedback);
            return { status:false, error:lastError, feedback:lastFeedback };
        }

        var formattedHtml = "<div><pre style=\"" + formatStyle + "\">";
        formattedHtml += selectedText;
        formattedHtml += "</pre></div>";

        this.pushUndo(document.body, document.body.innerHTML);
        return this.performAction('insertHtml', formattedHtml);
    }

    this.unwrap = function(element) {
        // When already inside the source code block, need to remove the source code block formatting
        var containedText = element.textContent;
        console.log("Contained text: " + containedText);

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

        this.pushUndo(document.body, document.body.innerHTML);

        observer.stop();
        try {
            divParent.removeChild(div);

            var range = document.createRange();
            range.selectNode(newSelectionNode);
            range.collapse();

            var selection = window.getSelection();
            selection.removeAllRanges();
            selection.addRange(range);
        }
        finally {
            observer.start();
        }

        return this.performAction('insertText', containedText);
    }

    this.performAction = function(action, arg) {
        var gotError = false;

        observer.stop();
        try {
            document.execCommand(action, false, arg);
        }
        catch(e) {
            lastError = e.message;
            lastFeedback = "";
            console.warn("Caught exception: " + lastError);
            gotError = true;
        }
        finally {
            observer.start();
        }

        if (gotError) {
            document.body = this.popUndo()[0];
            return { status:false, error:lastError, feedback:lastFeedback };
        }

        return { status:true, error:"", lastFeedback:"" };
    }
}

(function() {
    SourceCodeFormatter.prototype = Object.create(NodeUndoRedoManager.prototype);
    window.sourceCodeFormatter = new SourceCodeFormatter;
})();
