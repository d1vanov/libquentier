function disableKeyboardModifiers(e) {
    if (e.ctrlKey || e.altKey) {
        console.log("Detected ctrl or alt key, preventing the default");
        e.preventDefault();
    }
}

document.onkeypress = disableKeyboardModifiers;

