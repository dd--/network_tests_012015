/**
 * XULSchoolChrome namespace.
 */
if ("undefined" == typeof(networktest)) {
  var networktest = {};
};

/**
 * Controls the browser overlay for the Hello World extension.
 */
networktest.BrowserOverlay = {
  /**
   * Says 'Hello' to the user.
   */
  sayHello : function() {
    let stringBundle = document.getElementById("networktest-string-bundle");
    let message = stringBundle.getString("answer");

    window.alert(message);
  }
};
