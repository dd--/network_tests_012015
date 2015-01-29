/**
 * XULSchoolChrome namespace.
 */
if ("undefined" == typeof(networktestrunner)) {
  var networktestrunner = {};
};

/**
 * Controls the browser overlay for the Hello World extension.
 */
networktestrunner.BrowserOverlay = {

  runTest : function() {
    let netTest = Components.classes["@mozilla.org/network-test;1"]
                    .getService(Components.interfaces.NetworkTest);
    netTest.runTest();
    let stringBundle = document.getElementById("networktest-string-bundle");
    let message = stringBundle.getString("answer");

    window.alert(message);
  }
};
