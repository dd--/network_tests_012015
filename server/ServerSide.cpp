#include "TCPserver.h"
#include "prlog.h"

PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gVPNLog, PR_LOG_DEBUG, args)

int
main(int32_t argc, char *argv[])
{
  gServerTestLog = PR_NewLogModule("NetworkTestServer");
  TCPserver tcp;
  uint16_t ports[] = { 80, 891, 519, 2780, 4000, 443 };

  tcp.Start(ports, 6);
}
