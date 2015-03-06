/**
 * XULSchoolChrome namespace.
 */
if ("undefined" == typeof(networktestrunner)) {
  var networktestrunner = {};
};

var listener = {
  testsFinished : function() {
    let stringBundle = document.getElementById("networktest-string-bundle");
    let message = stringBundle.getString("answer");

    window.alert(message);
  },
  QueryInterface: function(aIID) {
    if (aIID.equals(Ci.NetworkTestListener) ||
        aIID.equals(Ci.nsISupports)) {
      return this;
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  }
}

/**
 * Controls the browser overlay for the Hello World extension.
 */
networktestrunner.BrowserOverlay = {

  runTest : function() {
    let netTest = Components.classes["@mozilla.org/network-test;1"]
                    .getService(Components.interfaces.NetworkTest);
    netTest.runTest(listener);
  }
};
