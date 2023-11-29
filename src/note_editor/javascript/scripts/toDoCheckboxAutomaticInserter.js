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

    this.insertToDo = function(id) {
        console.log("ToDoCheckboxAutomaticInserter::insertToDo: id = " + id);

        var res = null;
        observer.stop();
        try {
            res = this.insertToDoImpl(id);
        }
        finally {
            observer.start();
        }

        return res;
    }

    this.insertToDoImpl = function(id) {
        var selection = window.getSelection();
        if (!selection) {
            return { status:false, error:"No selection" };
        }

        var anchorNode = selection.anchorNode;
        if (!anchorNode) {
            return { status:false, error:"No anchor node within the selection" };
        }

        console.log("Original anchor node: " + this.nodeToString(anchorNode));

        this.pushUndo(anchorNode.parentNode, anchorNode.parentNode.innerHTML);

        if (!selection.isCollapsed) {
            console.log("Collapsing the selection by inserting empty text");
            document.execCommand('insertText', false, "");
        }

        anchorNode = selection.anchorNode;

        if ((anchorNode.nodeType == 1) && selection.anchorOffset) {
            anchorNode = anchorNode.childNodes[Math.min(selection.anchorOffset, anchorNode.childNodes.length-1)];
            if (!anchorNode) {
                return { status:false, error:"No innermost anchor node" };
            }

            while(anchorNode.childNodes.length) {
                anchorNode = anchorNode.childNodes[anchorNode.childNodes.length-1];
            }

            console.log("Innermost found anchor node: " + this.nodeToString(anchorNode));
        }

        var newCheckbox = document.createElement("img");
        newCheckbox.src = "qrc:/checkbox_icons/checkbox_no.png";
        newCheckbox.className = "checkbox_unchecked";
        newCheckbox.setAttribute("en-tag", "en-todo");
        newCheckbox.setAttribute("en-todo-id", id);
        newCheckbox.onclick = onEnToDoTagClick;

        if (anchorNode.nodeType == 3) {
            var offset = selection.anchorOffset;
            if (!offset) {
                offset = 0;
            }

            var leftText = anchorNode.textContent.substring(0, offset);
            var rightText = anchorNode.textContent.substring(offset, anchorNode.textContent.length);

            console.log("Offset = " + offset + ", text content length = " + anchorNode.textContent.length);
            console.log("Text content: " + anchorNode.textContent);
            console.log("Left text: " + leftText);
            console.log("Right text: " + rightText);

            var anchorParentNode = anchorNode.parentNode;
            console.log("Anchor parent node before: " + this.nodeToString(anchorParentNode));

            if (leftText != "") {
                var leftTextNode = document.createTextNode(leftText);
                anchorParentNode.insertBefore(leftTextNode, anchorNode);
            }

            anchorParentNode.insertBefore(newCheckbox, anchorNode);

            if (rightText != "") {
                var rightTextNode = document.createTextNode(rightText);
                anchorParentNode.insertBefore(rightTextNode, anchorNode);
            }

            anchorParentNode.removeChild(anchorNode);
            console.log("Anchor parent node after: " + this.nodeToString(anchorParentNode));
        }
        else {
            if (anchorNode.nodeType != 1) {
                console.log("Going up from node with type " + anchorNode.nodeType);
                anchorNode = anchorNode.parentNode;
            }

            while(anchorNode.nodeName == "BR") {
                console.log("Going up from BR node");
                anchorNode = anchorNode.parentNode;
            }

            console.log("Using parent node for checkbox: " + this.nodeToString(anchorNode));
            anchorNode.appendChild(newCheckbox);
        }

        var range = document.createRange();
        range.selectNode(newCheckbox);
        range.collapse(false);
        selection.removeAllRanges();
        selection.addRange(range);
        return { status:true, error:"" };
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

        console.log("Initial selection anchor node: " + this.nodeToString(currentNode));

        if (currentNode.nodeType == 1) {
            if (selection.anchorOffset) {
                console.log("Selection anchor offset = " + selection.anchorOffset + ", selection anchor node has " +
                            currentNode.childNodes.length + " child nodes");
                currentNode = currentNode.childNodes[Math.min(selection.anchorOffset, currentNode.childNodes.length-1)];
                if (!currentNode) {
                    console.log("No node at selection anchor offset of " + selection.anchorOffset);
                    return;
                }
                console.log("Current node after processing the anchor offset: " + this.nodeToString(currentNode));
            }

            while(currentNode.childNodes.length) {
                currentNode = currentNode.childNodes[currentNode.childNodes.length-1];
                console.log("Moved to the last child node: " + this.nodeToString(currentNode));
            }
        }

        console.log("Startup node: " + this.nodeToString(currentNode));

        var foundTextBetweenCheckboxAndSelection = false;
        var foundCheckboxAtLineBeginning = false;
        var lastElementNode = null;

        while (currentNode) {
            console.log("Inspecting node: " + this.nodeToString(currentNode));
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
            console.log("Previous sibling node: " + this.nodeToString(currentNode));

            if (!currentNode) {
                currentNode = parentNode;
                console.log("No previous sibling node, moved to parent node: " + this.nodeToString(currentNode));
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

            this.pushUndo(lastElementNode.parentNode, lastElementNode.parentNode.innerHTML);

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

        maxEnToDo++;

        observer.stop();
        try {
            if (lastElementNode.parentNode.nodeName == "DIV") {
                console.log("Last element node's parent node is DIV");
                this.pushUndo(lastElementNode.parentNode.parentNode,
                              lastElementNode.parentNode.parentNode.innerHTML);
                var newDiv = document.createElement("div");
                if (lastElementNode.parentNode.nextSibling) {
                    lastElementNode.parentNode.parentNode.insertBefore(newDiv, lastElementNode.parentNode.nextSibling);
                }
                else {
                    lastElementNode.parentNode.parentNode.appendChild(newDiv);
                }
                var newCheckbox = document.createElement("img");
                newCheckbox.src = "qrc:/checkbox_icons/checkbox_no.png";
                newCheckbox.className = "checkbox_unchecked";
                newCheckbox.setAttribute("en-tag", "en-todo");
                newCheckbox.setAttribute("en-todo-id", maxEnToDo.toString());
                newCheckbox.onclick = onEnToDoTagClick;
                newDiv.appendChild(newCheckbox);
                var newRange = document.createRange();
                newRange.selectNode(newCheckbox);
                newRange.collapse(false);
                selection.removeAllRanges();
                selection.addRange(newRange);
            }
            else {
                console.log("Last element node's parent node is not DIV");
                var htmlToInsert = "<br>";
                htmlToInsert += "<img src=\"qrc:/checkbox_icons/checkbox_no.png\" class=\"checkbox_unchecked\" ";
                htmlToInsert += "en-tag=\"en-todo\" en-todo-id=\"";
                htmlToInsert += (maxEnToDo + 1).toString();
                htmlToInsert += "\">";
                this.pushUndo(lastElementNode.parentNode.parentNode, lastElementNode.parentNode.parentNode.innerHTML);
                document.execCommand('insertHTML', false, htmlToInsert);

                var newCheckbox = document.querySelectorAll('[en-todo-id="' + (maxEnToDo + 1).toString() + '"]');
                if (newCheckbox.length == 1) {
                    newCheckbox[0].onclick = onEnToDoTagClick;
                }
            }
        }
        finally {
            observer.start();
        }

        toDoCheckboxAutomaticInsertionHandler.onToDoCheckboxInsertedAutomatically();
        return false;
    }
}

(function() {
    ToDoCheckboxAutomaticInserter.prototype = Object.create(NodeUndoRedoManager.prototype);
    window.toDoCheckboxAutomaticInserter = new ToDoCheckboxAutomaticInserter;
    document.body.addEventListener("keypress", function(e) { toDoCheckboxAutomaticInserter.checkKeypressAndInsertToDo(e); }, false);
})();
