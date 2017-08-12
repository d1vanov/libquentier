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

function ToDoCheckboxAutomaticInserter() {
    NodeUndoRedoManager.call(this);

    this.isLineBreakingNode = function(n) {
        if (!n) {
            return false;
        }

        if ((n.nodeType == 1) && ((n.nodeName == "DIV") || (n.nodeName == "BR") || (n.nodeName == "BODY"))) {
            return true;
        }

        return false;
    }

    this.isCheckboxNode = function(n) {
        if (!n) {
            return false;
        }

        if (n.nodeType != 1) {
            return false;
        }

        if ((n.nodeName == "IMG") && n.hasAttribute("en-tag") && (n.getAttribute("en-tag") == "en-todo")) {
            return true;
        }

        return false;
    }

    this.nodeToString = function(n) {
        var str = "";
        if (!n) {
            return str;
        }

        str = "Node type = ";
        str += n.nodeType;

        if (n.nodeType == 1) {
            str += ", name = " + n.nodeName + ", contents: " + n.innerHTML;
        }
        else if (n.nodeType == 3) {
            str += ", contents: " + n.textContent;
        }

        return str;
    }

    this.checkKeypressAndInsertToDo = function(e) {
        if (e.keyCode != 13) {
            return;
        }

        var selection = window.getSelection();
        if (!selection) {
            return;
        }

        if (!selection.isCollapsed) {
            return;
        }

        console.log("ToDoCheckboxAutomaticInserter::checkKeypressAndInsertToDo");

        var currentNode = selection.anchorNode;
        if (!currentNode) {
            console.log("Selection has no anchor node");
            return;
        }

        if ((currentNode.nodeType == 3) && (selection.anchorOffset != currentNode.textContent.length)) {
            console.log("Selection is somewhere within the text node");
            return;
        }

        if ((currentNode.nodeType == 1) && selection.anchorOffset) {
            currentNode = currentNode.childNodes[selection.anchorOffset];
            if (!currentNode) {
                console.log("No node at selection anchor offset of " + selection.anchorOffset);
                return;
            }

            if (currentNode.nextSibling && !this.isLineBreakingNode(currentNode.nextSibling)) {
                currentNode = currentNode.nextSibling;
            }
            else {
                while(this.isLineBreakingNode(currentNode) && currentNode.previousSibling) {
                    console.log("Shifting to the previous sibling of a line breaking node: " + this.nodeToString(currentNode));
                    currentNode = currentNode.previousSibling;
                }
            }
        }

        var foundTextBetweenCheckboxAndSelection = false;
        var foundCheckboxAtLineBeginning = false;
        var lastElementNode = null;

        while (currentNode) {
            if (currentNode.nodeType == 1) {
                if (this.isLineBreakingNode(currentNode)) {
                    console.log("Found line breaking node: " + currentNode.outerHTML);

                    if (lastElementNode && (lastElementNode.nodeName == "INPUT") && (lastElementNode.getAttribute("type") == "checkbox")) {
                        console.log("The last found element node before the line break is the checkbox node");
                        foundCheckboxAtLineBeginning = true;
                    }

                    break;
                }
                else if (this.isCheckboxNode(currentNode)) {
                    console.log("Found checkbox node");
                }

                lastElementNode = currentNode;
            }
            else if ((currentNode.nodeType == 3) && currentNode.textContent && (currentNode.textContent.replace(/(\r\n|\n|\r|\s+)/gm, "") != "")) {
                console.log("Found non-empty text node: " + currentNode.textContent);
                foundTextBetweenCheckboxAndSelection = true;
            }

            var parentNode = currentNode.parentNode;

            currentNode = currentNode.previousSibling;
            if (!currentNode) {
                currentNode = parentNode;
            }
        }

        if (!foundCheckboxAtLineBeginning && this.isCheckboxNode(lastElementNode)) {
            console.log("The last node found before the loop end was the checkbox one");
            foundCheckboxAtLineBeginning = true;
        }

        if (!foundCheckboxAtLineBeginning) {
            console.log("Found no checkbox at line beginning");
            return;
        }

        e.preventDefault();

        if (!foundTextBetweenCheckboxAndSelection) {
            console.log("Found no text between checkbox and the cursor position, should remove the checkbox node");

            observer.stop();
            try {
                if (!lastElementNode.nextSibling || (lastElementNode.nextSibling.nodeType != 1) || (lastElementNode.nextSibling.nodeName != "BR")) {
                    document.execCommand('insertHTML', false, '<br>');
                }
                lastElementNode.parentNode.removeChild(lastElementNode);
            }
            finally {
                observer.start();
            }

            toDoCheckboxAutomaticInsertionHandler.onToDoCheckboxInsertedAutomatically();
            return;
        }

        console.log("Detected line break introduction from line which starts with a checkbox and has some text after the checkbox, need to insert line break and insert a checkbox on the next line");

        var maxEnToDo = 1;
        var toDoTags = document.querySelectorAll('img[en-todo-id]:not([en-todo-id=""])');
        for(var i = 0; i < toDoTags.length; i++) {
            var currentEnToDoId = parseInt(toDoTags[i].getAttribute("en-todo-id"));
            if (maxEnToDo < currentEnToDoId) {
                maxEnToDo = currentEnToDoId;
            }
        }

        var htmlToInsert = "<br>";
        htmlToInsert += "<img src=\"qrc:/checkbox_icons/checkbox_no.png\" class=\"checkbox_unchecked\" ";
        htmlToInsert += "en-tag=\"en-todo\" en-todo-id=\"";
        htmlToInsert += (maxEnToDo + 1).toString();
        htmlToInsert += "\">";

        observer.stop();
        try {
            document.execCommand('insertHTML', false, htmlToInsert);
        }
        finally {
            observer.start();
        }

        toDoCheckboxAutomaticInsertionHandler.onToDoCheckboxInsertedAutomatically();
        return false;
    }
}
