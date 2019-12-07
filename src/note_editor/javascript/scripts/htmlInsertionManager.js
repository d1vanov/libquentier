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

/**
 * This function implements the JavaScript side control over html insertion
 * and corresponding undo/redo commands
 */

function HtmlInsertionManager() {
    var undoNodes = [];
    var undoNodeInnerHtmls = [];

    var redoNodes = [];
    var redoNodeInnerHtmls = [];

    var lastError;

    this.insertHtml = function(inputHtml)
    {
        console.log("HtmlInsertionManager::insertHtml: " + inputHtml);

        undoNodes.push(document.body);
        undoNodeInnerHtmls.push(document.body.innerHTML);

        var gotError = false;
        var errorText = ""

        observer.stop();
        try {
            document.execCommand('insertHtml', false, inputHtml);
        }
        catch(e) {
            errorText = e.message;
            console.warn("Caught exception while trying to insert HTML: " + errorText);
            gotError = true;
        }
        finally {
            observer.start();
        }

        if (gotError) {
            this.undo();
            return { status:false, error:errorText };
        }

        return { status:true, error:"" };
    }

    this.undo = function() {
        return this.undoRedoImpl(undoNodes, undoNodeInnerHtmls,
                                redoNodes, redoNodeInnerHtmls, true);

    }

    this.redo = function() {
        return this.undoRedoImpl(redoNodes, redoNodeInnerHtmls,
                                 undoNodes, undoNodeInnerHtmls, false);
    }

    this.undoRedoImpl = function(sourceNodes, sourceNodeInnerHtmls,
                                 destNodes, destNodeInnerHtmls, performingUndo) {
        console.log("HtmlInsertionManager::undoRedoImpl: performingUndo = " + (performingUndo ? "true" : "false"));

        var actionString = (performingUndo ? "undo" : "redo");

        if (!sourceNodes) {
            lastError = "can't " + actionString + " the resource action: no source nodes helper array";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        if (!sourceNodeInnerHtmls) {
            lastError = "can't " + actionString + " the resource action: no source node inner html helper array";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var sourceNode = sourceNodes.pop();
        if (!sourceNode) {
            lastError = "can't " + actionString + " the resource action: no source node";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var sourceNodeInnerHtml = sourceNodeInnerHtmls.pop();
        if (!sourceNodeInnerHtml) {
            lastError = "can't " + actionString + " the resource action: no source node's inner html";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        destNodes.push(sourceNode);
        destNodeInnerHtmls.push(sourceNode.innerHTML);

        console.log("Html before: " + sourceNode.innerHTML + "; html to paste: " + sourceNodeInnerHtml);

        observer.stop();
        try {
            sourceNode.innerHTML = sourceNodeInnerHtml;
        }
        finally {
            observer.start();
        }

        return { status:true, error:"" };
    }
}

(function() {
    window.htmlInsertionManager = new HtmlInsertionManager;
})();
