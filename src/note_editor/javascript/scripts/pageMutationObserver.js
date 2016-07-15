/*
 * Copyright 2016 Dmitry Ivanov
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

MutationObserver = window.MutationObserver || window.WebKitMutationObserver;

var observer = new MutationObserver(function(mutations, observer) {
    if (!window.hasOwnProperty('pageMutationObserver')) {
        console.log("pageMutationObserver global variable is not defined yet");
        return;
    }

    var numMutations = mutations.length;
    if (!numMutations) {
        return;
    }

    console.log("Detected " + numMutations + " page mutations");

    var numApprovedMutations = 0;

    for(var index = 0; index < numMutations; ++index) {
        var mutation = mutations[index];

        var numAddedNodes = (mutation.addedNodes ? mutation.addedNodes.length : 0);
        var numRemovedNodes = (mutation.removedNodes ? mutation.removedNodes.length : 0);

        console.log("Mutation[" + index + "]: type = " + mutation.type + ", target: " +
                    (mutation.target ? mutation.target.nodeName : "null") + ", num added nodes = " +
                    numAddedNodes + ", num removed nodes = " + numRemovedNodes);

        if (mutation.type === "characterData") {
            console.log("Character data mutation: old value = " + mutation.oldValue + ", new value = " + mutation.target.nodeValue);
        }

        if (numAddedNodes) {
            for(var addedNodesIndex = 0; addedNodesIndex < numAddedNodes; ++addedNodesIndex) {
                var addedNode = mutation.addedNodes[addedNodesIndex];
                console.log("Added node[" + addedNodesIndex + "]: name = " + addedNode.nodeName);
                var attributes = addedNode.attributes;
                if (attributes) {
                    for(var attributesIndex = 0; attributesIndex < attributes.length; ++attributesIndex) {
                        console.log("attribute: " + attributes[attributesIndex].name + ": " + attributes[attributesIndex].value);
                    }
                }
            }
        }

        if (numRemovedNodes) {
            for(var removedNodesIndex = 0; removedNodesIndex < numRemovedNodes; ++removedNodesIndex) {
                var removedNode = mutation.removedNodes[removedNodesIndex];
                console.log("Removed node[" + removedNodesIndex + "]: name = " + removedNode.nodeName);
                attributes = removedNode.attributes;
                if (attributes) {
                    for(attributesIndex = 0; attributesIndex < attributes.length; ++attributesIndex) {
                        console.log("attribute: " + attributes[attributesIndex].name + ": " + attributes[attributesIndex].value);
                    }
                }
            }
        }

        if (mutation.type === "childList") {
            if (mutation.addedNodes && mutation.addedNodes.length &&
                mutation.removedNodes && mutation.removedNodes.length) {
                console.log("Skipping child mutation which contains both added and removed nodes: " +
                            "it doesn't look like manual note text editing");
                continue;
            }
            else if (mutation.target && mutation.target.nodeName === "HEAD") {
                console.log("Skipping the mutation of HEAD target");
                continue;
            }
            else if (mutation.addedNodes && (mutation.addedNodes.length === 1)) {
                addedNode = mutation.addedNodes[0];
                if (addedNode) {
                    if (addedNode.nodeName === "#text") {
                        console.log("Skipping the addition of #text node");
                        continue;
                    }
                    else if (addedNode.attributes) {
                        var enTagAttribute = addedNode.attributes.getNamedItem("en-tag");
                        if (enTagAttribute && enTagAttribute.value === "en-crypt") {
                            console.log("Skipping the addition of en-crypt node");
                            continue;
                        }

                        var classAttribute = addedNode.attributes.getNamedItem("class");
                        if (classAttribute && classAttribute.value === "hilitorHelper") {
                            console.log("Skipping the addition of hilitor helper node");
                            continue;
                        }
                    }
                }
            }
            else if (mutation.removedNodes && (mutation.removedNodes.length === 1)) {
                removedNode = mutation.removedNodes[0];
                if (removedNode && removedNode.attributes) {
                    var classAttribute = removedNode.attributes.getNamedItem("class");
                    if (classAttribute && classAttribute.value === "hilitorHelper") {
                        console.log("Skipping the removal of hilitor helper node");
                        continue;
                    }
                }
            }

            ++numApprovedMutations;
            textEditingUndoRedoManager.pushNode(mutation.target.parentNode);
        }
        else if (mutation.type !== "characterData") {
            console.log("Skipping non-characterData mutation");
            continue;
        }
        else {
            ++numApprovedMutations;
            textEditingUndoRedoManager.pushNode(mutation.target, mutation.oldValue);
        }
    }

    if (numApprovedMutations) {
        textEditingUndoRedoManager.pushNumMutations(numApprovedMutations);
        pageMutationObserver.onPageMutation();
    }
});

observer["running"] = false;

observer.start = function() {
    this.observe(document, {
        subtree: true,
        attributes: true,
        childList: true,
        characterData: true,
        characterDataOldValue: true
    });

    this.running = true;
}

observer.stop = function() {
    this.disconnect();
    this.running = false;
}

observer.start();
