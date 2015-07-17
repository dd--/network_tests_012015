self.port.on("reachability", function(result) {
console.log("gotovo 2");

  var ports = JSON.parse(result[0]);
  var tcps = JSON.parse(result[1]);
  var udps = JSON.parse(result[2]);
  dump("DDDDD 1 "+ ports+ " "+ ports[0] +" "+ tcps[1] +"\n");

  var test = document.getElementById("reachability");
  test.innerHTML = "Reachability test finished";
  var udpDesc = document.createElement("p");
  udpDesc.innerHTML = "UDP resultes:";
  var tcpDesc = document.createElement("p");
  tcpDesc.innerHTML = "TCP resultes:";

  var udpList = document.createElement("ul");
  var tcpList = document.createElement("ul");

  for (var i = 0; i < ports.length; i++) {
    var el = document.createElement("li");
    el.innerHTML = "Port " + ports[i] + ((udps[i]) ? " is " : " is not ") +
                   "reachable using UDP protocol";
    udpList.appendChild(el);

    var el = document.createElement("li");
    el.innerHTML = "Port " + ports[i] + ((tcps[i] == "true") ? " is " : " is not ") + 
                   "reachable using TCP protocol";
    tcpList.appendChild(el);
  }

  var testResults = document.getElementById("reachabilityResults");
  testResults.appendChild(udpDesc);
  testResults.appendChild(udpList);
  testResults.appendChild(tcpDesc);
  testResults.appendChild(tcpList);

});