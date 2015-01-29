#ifndef TEST_TCPSERVER_H
#define TEST_TCPSERVER_H

#include "prio.h"


class TCPserver
{
public:
  TCPserver();
  ~TCPserver();
  int Start(uint16_t *aPort, int aNumberOfPorts);

private:
  int Init(uint16_t aPort, int aInx);
  int Run();
  int StartClientThread(PRFileDesc *fdClient);

  PRFileDesc **mFds;
  int mNumberOfPorts;
};

#endif
