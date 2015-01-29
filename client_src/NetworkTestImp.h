#include "NetworkTest.h"
#include "prnetdb.h"
#include "nsString.h"
#include "nsAutoPtr.h" 

class NetworkTestImp : public NetworkTest
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NetworkTestImp();
  ~NetworkTestImp();
  NS_IMETHOD RunTest();

private:
  int GetHostAddr(nsAutoCString &aAddr);
  nsresult GetNextAddr(PRNetAddr *aAddr);
  void AddPort(PRNetAddr *aAddr, uint16_t aPort);
  nsresult Test1(PRNetAddr *aNetAddr);
  nsresult Test2(PRNetAddr *aNetAddr);
  nsresult Test3(PRNetAddr *aNetAddr, uint16_t aPort);

  PRAddrInfo *mAddrInfo;
  void *mIter;
  bool *mTCPReachabilityResults;
  bool *mUDPReachabilityResults;
};
