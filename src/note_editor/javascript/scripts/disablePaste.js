function disablePaste(e) {
    console.log("disablePaste");
    e.preventDefault();
}

document.body.onpaste = disablePaste;

