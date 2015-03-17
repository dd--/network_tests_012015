let { Cc, Ci } = require("chrome");
var buttons = require('sdk/ui/button/action');
var tabs = require("sdk/tabs");

var button = buttons.ActionButton({
  id: "network-tester",
  label: "Start network tester",
  icon: {
    "16": "./icon-16.png",
    "32": "./icon-32.png",
    "64": "./icon-64.png"
  },
  onClick: handleClick
});

var listener = {
  testsFinished : function() {
    console.log("Network test finished");
  },
  QueryInterface: function(aIID) {
    if (aIID.equals(Ci.NetworkTestListener) ||
        aIID.equals(Ci.nsISupports)) {
      return this;
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  }
};

function handleClick(state) {
  console.log("launching network tester");
  let netTest = Cc["@mozilla.org/network-test;1"].getService(Ci.NetworkTest);
  netTest.runTest(listener);
}
