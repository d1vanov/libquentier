function disablePaste(e) {
    console.log("disablePaste");
    e.preventDefault();
    actionsWatcher.onPasteActionToggled();
}

function disableCut(e) {
    console.log("disableCut");
    e.preventDefault();
    actionsWatcher.onCutActionToggled();
}

document.body.onpaste = disablePaste;
document.body.oncut = disableCut;
