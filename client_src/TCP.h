#ifndef TEST_TCP_CONNECTION
#define TEST_TCP_CONNECTION

#include "prerror.h"
#include "prio.h"

nsresult ErrorAccordingToNSPR();
nsresult ErrorAccordingToNSPRWithCode(PRErrorCode errCode);

class TCP
{
public:
  TCP(PRNetAddr *aAddr);
  ~TCP();
  nsresult Start(int aTestType);
private:
  nsresult Init();
  nsresult Run();

  PRNetAddr mNetAddr;
  PRFileDesc *mFd;
  int mTestType;
};

#endif
