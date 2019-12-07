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

function managedPageAction(command, args) {
    console.log("managedPageAction: command = " + command + ", args = " + args);

    var selection = window.getSelection();
    if (!selection) {
        return { status: false, error: "No selection" };
    }

    var targetNode = selection.anchorNode;
    while(targetNode && (targetNode.nodeType === 3)) {
        targetNode = targetNode.parentNode;
    }

    if (!targetNode) {
        return { status: false, error: "No target node" };
    }

    if (targetNode !== document.body) {
        targetNode = targetNode.parentNode;
        if (!targetNode) {
            return { status: false, error: "No target node's parent" };
        }
    }

    if (targetNode !== document.body) {
        targetNode = targetNode.parentNode;
        if (!targetNode) {
            return { status: false, error: "No target node's grand parent" };
        }
    }

    var targetNodeHtml = targetNode.innerHTML;

    textEditingUndoRedoManager.pushNode(targetNode, targetNodeHtml);
    textEditingUndoRedoManager.pushNumMutations(1);

    var shouldRestoreSelection = (command != 'insertText') &&
                                 (command != 'cut') &&
                                 (command != 'insertUnorderedList') &&
                                 (command != 'insertOrderedList') &&
                                 (command != 'indent') &&
                                 (command != 'outdent')
    observer.stop();
    try {
        var savedSelection;
        if (shouldRestoreSelection) {
            savedSelection = selectionManager.saveSelection();
        }

        if (command === 'cut') {
            // This command doesn't seem to really work so need to emulate
            document.execCommand("insertHTML", false, "");
        }
        else {
            document.execCommand(command, false, args);
        }

        if (shouldRestoreSelection) {
            selectionManager.restoreSelection(savedSelection);
        }
    }
    finally {
        observer.start();
    }

    return { status: true, error: "" };
}
