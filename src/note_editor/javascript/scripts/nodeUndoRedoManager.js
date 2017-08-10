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

function NodeUndoRedoManager() {
    this.undoNodes = [];
    this.undoNodeInnerHtmls = [];

    this.redoNodes = [];
    this.redoNodeInnerHtmls = [];

    this.lastError;

    this.undoRedoImpl = function(sourceNodes, sourceNodeInnerHtmls, destNodes, destNodeInnerHtmls, performingUndo) {
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

NodeUndoRedoManager.prototype.undo = function() {
    console.log("NodeUndoRedoManager: undo");
    return this.undoRedoImpl(this.undoNodes, this.undoNodeInnerHtmls,
                             this.redoNodes, this.redoNodeInnerHtmls, true);
}

NodeUndoRedoManager.prototype.redo = function() {
    console.log("NodeUndoRedoManager: redo");
    return this.undoRedoImpl(this.redoNodes, this.redoNodeInnerHtmls,
                             this.undoNodes, this.undoNodeInnerHtmls, false);
}

NodeUndoRedoManager.prototype.pushUndo = function(node, html) {
    this.undoNodes.push(node);
    this.undoNodeInnerHtmls.push(html);
}

NodeUndoRedoManager.prototype.pushRedo = function(node, html) {
    this.redoNodes.push(node);
    this.redoNodeInnerHtmls.push(html);
}

NodeUndoRedoManager.prototype.popUndo = function() {
    var node = undoNodes.pop();
    var html = undoNodeInnerHtmls.pop();
    var res = [];
    res.push(node);
    res.push(html);
    return res;
}

NodeUndoRedoManager.prototype.popRedo = function() {
    var node = redoNodes.pop();
    var html = redoNodeInnerHtmls.pop();
    res.push(node);
    res.push(html);
    return res;
}

NodeUndoRedoManager.prototype.getLastError = function () {
    return this.lastError;
}
