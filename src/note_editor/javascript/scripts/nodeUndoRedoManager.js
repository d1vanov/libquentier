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

function NodeUndoRedoManager() {
    this.undoNodes = [];
    this.undoNodeInnerHtmls = [];
    this.undoSavedSelections = [];

    this.redoNodes = [];
    this.redoNodeInnerHtmls = [];
    this.redoSavedSelections = [];

    this.lastError;

    this.undoRedoImpl = function(sourceNodes, sourceNodeInnerHtmls, sourceSavedSelections,
                                 destNodes, destNodeInnerHtmls, destSavedSelections, performingUndo) {
        var actionString = (performingUndo ? "undo" : "redo");

        if (!sourceNodes) {
            lastError = "can't " + actionString + ": no source nodes helper array";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        if (!sourceNodeInnerHtmls) {
            lastError = "can't " + actionString + ": no source node inner html helper array";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        if (!sourceSavedSelections) {
            lastError = "can't " + actionString + ": no source saved selections array";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var sourceNode = sourceNodes.pop();
        if (!sourceNode) {
            lastError = "can't " + actionString + ": no source node";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var sourceNodeInnerHtml = sourceNodeInnerHtmls.pop();
        if (!sourceNodeInnerHtml) {
            lastError = "can't " + actionString + ": no source node's inner html";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        var sourceSavedSelection = sourceSavedSelections.pop();
        if (!sourceSavedSelection) {
            lastError = "can't " + actionString + ": no source saved selection";
            console.warn(lastError);
            return { status:false, error:lastError };
        }

        destNodes.push(sourceNode);
        destNodeInnerHtmls.push(sourceNode.innerHTML);
        destSavedSelections.push(selectionManager.saveSelection());

        console.log("Html before: " + sourceNode.innerHTML + "; html to paste: " + sourceNodeInnerHtml);

        observer.stop();
        try {
            sourceNode.innerHTML = sourceNodeInnerHtml;
            selectionManager.restoreSelection(sourceSavedSelection);
        }
        finally {
            observer.start();
        }

        return { status:true, error:"" };
    }
}

NodeUndoRedoManager.prototype.undo = function() {
    console.log("NodeUndoRedoManager: undo");
    return this.undoRedoImpl(this.undoNodes, this.undoNodeInnerHtmls, this.undoSavedSelections,
                             this.redoNodes, this.redoNodeInnerHtmls, this.redoSavedSelections, true);
}

NodeUndoRedoManager.prototype.redo = function() {
    console.log("NodeUndoRedoManager: redo");
    return this.undoRedoImpl(this.redoNodes, this.redoNodeInnerHtmls, this.redoSavedSelections,
                             this.undoNodes, this.undoNodeInnerHtmls, this.undoSavedSelections, false);
}

NodeUndoRedoManager.prototype.pushUndo = function(node, html) {
    console.log("NodeUndoRedoManager: pushUndo");
    this.undoNodes.push(node);
    this.undoNodeInnerHtmls.push(html);
    this.undoSavedSelections.push(selectionManager.saveSelection());
}

NodeUndoRedoManager.prototype.pushRedo = function(node, html) {
    console.log("NodeUndoRedoManager: pushRedo");
    this.redoNodes.push(node);
    this.redoNodeInnerHtmls.push(html);
    this.redoSavedSelections.push(selectionManager.saveSelection());
}

NodeUndoRedoManager.prototype.popUndo = function() {
    var node = undoNodes.pop();
    var html = undoNodeInnerHtmls.pop();
    var selection = undoSavedSelections.pop();
    var res = [];
    res.push(node);
    res.push(html);
    res.push(selection);
    return res;
}

NodeUndoRedoManager.prototype.popRedo = function() {
    var node = redoNodes.pop();
    var html = redoNodeInnerHtmls.pop();
    var selection = redoSavedSelections.pop();
    res.push(node);
    res.push(html);
    res.push(selection);
    return res;
}

NodeUndoRedoManager.prototype.getLastError = function () {
    return this.lastError;
}
