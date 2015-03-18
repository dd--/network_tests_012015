/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NetworkTestImp.h"
#include "TCP.h"
#include "UDP.h"
#include "mozilla/Module.h"
#include "prlog.h"
#include "nsThreadUtils.h"

PRLogModuleInfo* gClientTestLog;
#define LOG(args) PR_LOG(gClientTestLog, PR_LOG_DEBUG, args)

uint64_t maxBytes = (1<<21);
uint32_t maxTime = 4; //TODO:chnge tthis to the 12s

uint16_t ports[] = { 4230, 2708, 891, 519, 80, 443 };
uint16_t portsLocal[] = { 4231, 2709, 892, 520, 81, 444 };
int numberOfPorts = 6;


// todo pref
static nsAutoCString address(NS_LITERAL_CSTRING("localhost"));

NS_IMPL_ISUPPORTS(NetworkTestImp, NetworkTest)

NetworkTestImp::NetworkTestImp()
{
  mTCPReachabilityResults = new bool[numberOfPorts];
  mUDPReachabilityResults = new bool[numberOfPorts];
}

NetworkTestImp::~NetworkTestImp()
{
  PR_FreeAddrInfo(mAddrInfo);
  delete [] mTCPReachabilityResults;
  delete [] mUDPReachabilityResults;
}

void
NetworkTestImp::AllTests()
{
  // worker thread
  for (int inx = 0; inx < numberOfPorts; inx++) {
    mTCPReachabilityResults[inx] = false;
    mUDPReachabilityResults[inx] = false;
  }

  mIter = nullptr;
  gClientTestLog = PR_NewLogModule("NetworkTestClient");

  bool complete = false;

  LOG(("Get host addr."));
  if (GetHostAddr(address) != 0) {
    goto done;
  }

  PRNetAddr addr;
  if (NS_FAILED(GetNextAddr(&addr))) {
    goto done;
  }

  LOG(("Run test 1."));
  Test1(&addr);

  LOG(("Run test 2."));
  Test2(&addr);

  { // scoping for declaration and goto
    int portInx = -1;
    for (int inx = 0; inx < numberOfPorts; inx++) {
      if (mTCPReachabilityResults[inx] && mUDPReachabilityResults[inx]) {
        portInx = inx;
        break;
      }
    }
    if (portInx != -1) {
      Test3a(&addr, portsLocal[portInx], ports[portInx]);
      Test3b(&addr, portsLocal[portInx], ports[portInx]);
    }
  }

  complete = true;
  
done:
  LOG(("NetworkTest client side: Tests finished %s.", complete ? "ok" : "failed"));
  NS_DispatchToMainThread(NS_NewRunnableMethod(this, &NetworkTestImp::TestsFinished));
}

NS_IMETHODIMP
NetworkTestImp::RunTest(NetworkTestListener *aCallback)
{
  NS_ENSURE_ARG(aCallback);
  if (mCallback) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  mCallback = aCallback;
  nsresult rv = NS_NewThread(getter_AddRefs(mThread),
                             NS_NewRunnableMethod(this, &NetworkTestImp::AllTests));
  if (NS_FAILED(rv)) {
    LOG(("NetworkTest client side: Error creating the test thread"));
    return rv;
  }
  return NS_OK;
}

void
NetworkTestImp::TestsFinished()
{
  LOG(("NetworkTest client side: Shutdown thread."));
  if (mThread) {
    mThread->Shutdown();
  }

  nsCOMPtr<NetworkTestListener> callback;
  callback.swap(mCallback);
  callback->TestsFinished();
}

int
NetworkTestImp::GetHostAddr(nsAutoCString &aAddr)
{
  int flags = PR_AI_ADDRCONFIG;
  uint16_t af = PR_AF_UNSPEC;
  mAddrInfo = PR_GetAddrInfoByName(aAddr.get(), af, flags);
  return 0;
}

nsresult
NetworkTestImp::GetNextAddr(PRNetAddr *aAddr)
{
  mIter = PR_EnumerateAddrInfo(mIter, mAddrInfo, 0, aAddr);

  if (!mIter) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void
NetworkTestImp::AddPort(PRNetAddr *aAddr, uint16_t aPort)
{
  aPort = htons(aPort);
  if (aAddr->raw.family == AF_INET) {
    aAddr->inet.port = aPort;
  }
  else if (aAddr->raw.family == AF_INET6) {
    aAddr->ipv6.port = aPort;
  }
}

// UDP reachability
nsresult
NetworkTestImp::Test1(PRNetAddr *aNetAddr)
{
  nsresult rv;
  for (int inx = 0; inx < numberOfPorts; inx++) {
    LOG(("NetworkTest: Run test 1 with port %d.", ports[inx]));
    AddPort(aNetAddr, ports[inx]);
    UDP udp(aNetAddr, portsLocal[inx]);
    bool testSuccess = false;
    rv = udp.Start(1, 0, testSuccess);
    if (NS_FAILED(rv) || !testSuccess) {
      LOG(("NetworkTest: Run test 1 with port %d - failed.", ports[inx]));
    } else {
      mUDPReachabilityResults[inx] = true;
      LOG(("NetworkTest: Run test 1 with port %d - succeeded.", ports[inx]));
    }
  }
  return NS_OK;
}

// TCP reachability
nsresult
NetworkTestImp::Test2(PRNetAddr *aNetAddr)
{
  nsresult rv;
  for (int inx = 0; inx < numberOfPorts; inx++) {
    LOG(("NetworkTest: Run test 2 with port %d.", ports[inx]));
    AddPort(aNetAddr, ports[inx]);
    TCP tcp(aNetAddr);
    rv = tcp.Start(2);
    if (NS_FAILED(rv)) {
      LOG(("NetworkTest: Run test 2 with port %d - failed.", ports[inx]));
    } else {
      mTCPReachabilityResults[inx] = true;
      LOG(("NetworkTest: Run test 2 with port %d - succeeded.", ports[inx]));
    }
  }
  return NS_OK;
}

// Test 3 UDP vs TCP performance from a server to a client.
nsresult
NetworkTestImp::Test3a(PRNetAddr *aNetAddr, uint16_t aLocalPort,
                       uint16_t aRemotePort)
{
  LOG(("NetworkTest: Run test 3a with port %d.", aRemotePort));
  AddPort(aNetAddr, aRemotePort);
  TCP tcp(aNetAddr);
  bool testSuccess = false;
  nsresult rv;
  for (int iter = 0; iter < 10; iter++) {
    rv = tcp.Start(3);
    LOG(("Rate: %llu", tcp.GetRate()));
    if (NS_FAILED(rv)) {
      return rv;
    }
    UDP udp(aNetAddr, aLocalPort);
    rv = udp.Start(5, tcp.GetRate(), testSuccess);
    if (NS_FAILED(rv) && !testSuccess) {
      return rv;
    }
    LOG(("Rate: %llu", udp.GetRate()));
  }
  return rv;
}

// Test 3 UDP vs. TCP performance from a client to a server.
nsresult
NetworkTestImp::Test3b(PRNetAddr *aNetAddr, uint16_t aLocalPort,
                       uint16_t aRemotePort)
{
  LOG(("NetworkTest: Run test 3b with port %d.", aRemotePort));
  AddPort(aNetAddr, aRemotePort);
  TCP tcp(aNetAddr);
  bool testSuccess = false;
  nsresult rv;
  for (int iter = 0; iter < 10; iter++) {
    rv = tcp.Start(4);
    LOG(("Rate: %llu", tcp.GetRate()));
    if (NS_FAILED(rv)) {
      return rv;
    }
    UDP udp(aNetAddr, aLocalPort);
    rv = udp.Start(6, tcp.GetRate(), testSuccess);
    if (NS_FAILED(rv) && !testSuccess) {
      return rv;
    }
    LOG(("Rate: %llu", udp.GetRate()));
  }
  return rv;
}

static nsresult
NetworkTestContructor(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
  *aResult = nullptr;
  if (nullptr != aOuter) {
    return NS_ERROR_NO_AGGREGATION;
  }

  nsRefPtr<NetworkTestImp> inst = new NetworkTestImp();
  return inst->QueryInterface(aIID, aResult);
}

NS_DEFINE_NAMED_CID(NETWORKTEST_CID);

static const mozilla::Module::CIDEntry kNetworkTestCIDs[] = {
  { &kNETWORKTEST_CID, false, nullptr, NetworkTestContructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kNetworkTestContracts[] = {
  { NETWORKTEST_CONTRACTID, &kNETWORKTEST_CID },
  { nullptr }
};

static const mozilla::Module kNetworkTestModule = {
  mozilla::Module::kVersion,
  kNetworkTestCIDs,
  kNetworkTestContracts,
  nullptr,
  nullptr,
  nullptr,
  nullptr
};

NSMODULE_DEFN(NetworkTestModule) = &kNetworkTestModule;
