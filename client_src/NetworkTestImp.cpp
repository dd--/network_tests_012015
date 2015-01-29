#include "NetworkTestImp.h"
#include "TCP.h"
#include "mozilla/Module.h"
#include "prlog.h"

PRLogModuleInfo* gClientTestLog;
#define LOG(args) PR_LOG(gClientTestLog, PR_LOG_DEBUG, args)

// I will give test a code:
#define UDP_reachability "Test_1"
#define TCP_reachability "Test_2"
#define TCP_performanceFromServerToClient "Test_3"
#define TCP_performanceFromClientToServer "Test_4"
#define UDP_performanceFromServerToClient "Test_5"
#define UDP_performanceFromClientToServer "Test_6"

uint16_t ports[] = { 4230, 2708, 891, 519, 80, 443 };
int numberOfPorts = 6;

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

NS_IMETHODIMP
NetworkTestImp::RunTest()
{
  for (int inx = 0; inx < numberOfPorts; inx++) {
    mTCPReachabilityResults[inx] = false;
    mUDPReachabilityResults[inx] = false;
  }

  mIter = nullptr;
  gClientTestLog = PR_NewLogModule("NetworkTestClient");

  LOG(("Get host addr."));
  if (GetHostAddr(address) != 0) {
    return NS_ERROR_FAILURE;
  }

  PRNetAddr addr;
  nsresult rv = GetNextAddr(&addr);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  LOG(("Run test 2."));
  Test2(&addr);

  uint16_t port = 0;
  for (int inx = 0; inx < numberOfPorts; inx++) {
    if (mTCPReachabilityResults[inx]) {
      port = ports[inx];
      break;
    }
  }
  if (port) {
    Test3(&addr, port);
  }
  return NS_OK;
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

nsresult
NetworkTestImp::Test1(PRNetAddr *aNetAddr)
{
  return NS_OK;  
}

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

nsresult
NetworkTestImp::Test3(PRNetAddr *aNetAddr, uint16_t aPort)
{
  LOG(("NetworkTest: Run test 3 with port %d.", aPort));
  AddPort(aNetAddr, aPort);
  TCP tcp(aNetAddr);
  return tcp.Start(3);
}


static nsresult
NetworkTestContructor(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
  nsresult rv;
  NetworkTestImp *inst;
  *aResult = nullptr;
  if (nullptr != aOuter) {
    rv = NS_ERROR_NO_AGGREGATION;
    return rv;
  }

  inst = new NetworkTestImp();
  if (nullptr == inst) {
    rv = NS_ERROR_OUT_OF_MEMORY;
    return rv;
  }
  NS_ADDREF(inst);
  rv = inst->QueryInterface(aIID, aResult);
  NS_RELEASE(inst);
  return rv;
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
