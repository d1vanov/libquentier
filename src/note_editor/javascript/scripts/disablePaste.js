function disablePaste(e) {
    console.log("disablePaste");
    e.preventDefault();
    if (actionsWatcher) {
        actionsWatcher.onPasteActionToggled();
    }
}

function disableCut(e) {
    console.log("disableCut");
    e.preventDefault();
    if (actionsWatcher) {
        actionsWatcher.onCutActionToggled();
    }
}

document.body.onpaste = disablePaste;
document.body.oncut = disableCut;
