#include "prerror.h"
#include "prnetdb.h"
#include "TCPserver.h"
#include "prlog.h"
#include "prthread.h"
#include "prmem.h"
#include <cstring>

extern PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)
// after this short interval, we will return to PR_Poll
#define NS_SOCKET_CONNECT_TIMEOUT PR_MillisecondsToInterval(20)
#define SERVERSNDBUFFERSIZE 12582912

// I will give test a code:
#define UDP_reachability "Test_1"
#define TCP_reachability "Test_2"
#define TCP_performanceFromServerToClient "Test_3"
#define TCP_performanceFromClientToServer "Test_4"
uint64_t maxBytes = 3 * (1<<22);

void
LogErrorWithCode(PRErrorCode errCode)
{
  int errLen = PR_GetErrorTextLength();
  char *errStr = (char*)PR_MALLOC(errLen);
  if (errLen > 0) {
    PR_GetErrorText(errStr);
  }
  LOG(("NetworkTest TCP server side:  error %x %s, %x", errCode, errStr, PR_GetOSError()));
  delete [] errStr;
}

void
LogError()
{
    PRErrorCode errCode = PR_GetError();
      LogErrorWithCode(errCode);
}

static void PR_CALLBACK
ClientThread(void *_fd)
{
  LOG(("NetworkTest server side: Client thread created."));
  PRFileDesc *fd = (PRFileDesc*)_fd;

  PRPollDesc pollElem;
  pollElem.fd = fd;
  pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
  uint64_t writtenBytes = 0;
  int testType = 0;
  uint64_t readBytes = 0;
  char buf[1500];
  bool firstPktReceived = false;

  while (1) {
    pollElem.out_flags = 0;
    PR_Poll(&pollElem, 1, PR_INTERVAL_NO_WAIT);
    if (pollElem.out_flags & (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL)) {
      LogError();
      LOG(("NetworkTest server side: Sent %lu bytes, received %lu bytes",
           writtenBytes, readBytes));
      PR_Close(fd);
      return;
    }
    
    if (pollElem.out_flags & PR_POLL_READ) {
      int read;
      if (!firstPktReceived) {
        read = PR_Read(fd, buf + readBytes, 1500 - readBytes);
      } else {
        read = PR_Read(fd, buf, 1500);
      }

      if (read < 0) {
        PRErrorCode errCode = PR_GetError();
        if (errCode == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        LogErrorWithCode(errCode);
        PR_Close(fd);
        return;
      }

      readBytes += read;

      switch (testType) {
        case 0:
          if (readBytes > 6) {
            if (memcmp(buf, TCP_reachability, 6) == 0) {
              testType = 2;
              LOG(("NetworkTest server side: Starting test %d.", testType));
              if (readBytes >= 1500) {
                LOG(("NetworkTest server side: Test 2 data received."));
                pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
              }
            } else if (memcmp(buf, TCP_performanceFromServerToClient, 6) == 0) {
              testType = 3;
              LOG(("NetworkTest server side: Starting test %d.", testType));
              // First read the hole first packet before start sending.
              if (readBytes >= 1500) {
                firstPktReceived = true;
                LOG(("NetworkTest server side: Test 3 data received."));
                pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
              }
            } else {
              LOG(("NetworkTest server side: Test not implemented"));
              return;
            }
          }
          break;

        case 2:
          if (readBytes >= 1500) {
            LOG(("NetworkTest server side: Test 2 data received."));
            pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
          }
          break;
        case 3:
          if (readBytes >= 1500) {
            LOG(("NetworkTest server side: Test 3 the first packet "
                 "received, now we will start sending data."));
            firstPktReceived = true;
          }
          break;
        default:
          return;
      }
    } else  if (pollElem.out_flags & PR_POLL_WRITE) {
//          LOG(("NetworkTest server side: Write 1500 bytes."));
      int written = PR_Write(fd, buf, 1500);
      if (written < 0) {
        PRErrorCode errCode = PR_GetError();
        if (errCode == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        LogErrorWithCode(errCode);
        PR_Close(fd);
        return;
      }
      writtenBytes += written;
      switch (testType) {
        case 2:
          LOG(("NetworkTest server side: Test 2 %lu bytes sent.",
               writtenBytes));
          if (writtenBytes >= 1500) {
            pollElem.in_flags = PR_POLL_EXCEPT;
          }
          break;
        case 3:
          if (writtenBytes >= maxBytes) {
            LOG(("NetworkTest server side: Test 3 %lu bytes sent.",
                 writtenBytes));
            pollElem.in_flags = PR_POLL_EXCEPT;
          }
          break;

      default:
        return;
      }
    }
  }
}

TCPserver::TCPserver()
  : mFds(NULL)
  , mNumberOfPorts(0)
{
}

TCPserver::~TCPserver()
{
  for (int inx = 0; inx < mNumberOfPorts; inx++) {
    if (mFds[inx]) {
      PR_Close(mFds[inx]);
    }
  }
}

int
TCPserver::Start(uint16_t *aPort, int aNumberOfPorts)
{
  if (!(aNumberOfPorts > 0)) {
    return -1;
  }
  mFds = new PRFileDesc*[aNumberOfPorts];
  for (int inx = 0; inx < aNumberOfPorts; inx++) {
    mFds[inx] = NULL;
  }
  mNumberOfPorts = aNumberOfPorts;
  for (int inx = 0; inx < aNumberOfPorts; inx++) {
    int rv = Init(aPort[inx], inx);
    if (rv != 0 ) {
      return rv;
    }
  }
  return Run();
}

int
TCPserver::Init(uint16_t aPort, int aInx)
{
  LOG(("NetworkTest server side: Init socket: port %d", aPort));
  PRNetAddr addr;
  PRNetAddrValue val = PR_IpAddrAny;
  PRStatus status = PR_SetNetAddr(val, PR_AF_INET, aPort, &addr);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  char host[164] = {0};
  PR_NetAddrToString(&addr, host, sizeof(host));

  LOG(("NetworkTest server side: host %s", host));
  mFds[aInx] = PR_OpenTCPSocket(addr.raw.family);
  if (!mFds[aInx]) {
    LogError();
    return -1;
  }
  LOG(("NetworkTest server side: Socket opened."));

  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  opt.option = PR_SockOpt_Reuseaddr;
  opt.value.reuse_addr = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  opt.option = PR_SockOpt_NoDelay;
  opt.value.no_delay = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  opt.option = PR_SockOpt_SendBufferSize;
  opt.value.send_buffer_size = SERVERSNDBUFFERSIZE;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  LOG(("NetworkTest server side: Socket options set."));

  status = PR_Bind(mFds[aInx], &addr);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  LOG(("NetworkTest server side: Socket bind."));

  status = PR_Listen(mFds[aInx], 10);
  if (status != PR_SUCCESS) {
    LogError();
    return -1;
  }

  LOG(("NetworkTest server side: Socket listens."));
  return 0;
}

int
TCPserver::Run()
{
  PRNetAddr clientNetAddr;
  PRFileDesc *fdClient = NULL;
  while (1) {
    for (int inx =0; inx < mNumberOfPorts; inx++) {
      fdClient = PR_Accept(mFds[inx], &clientNetAddr, PR_INTERVAL_NO_WAIT);
      if (fdClient) {
        LOG(("NetworkTest server side: Client accepted."));
        int rv = StartClientThread(fdClient);
        if (rv != 0) {
          PR_Close(fdClient);
        }
        fdClient = NULL;
      }
    }
  }
  return 0;
}

int
TCPserver::StartClientThread(PRFileDesc *fdClient)
{
  PRThread *clientThread;
  clientThread = PR_CreateThread(PR_USER_THREAD, ClientThread,
                                 (void *)fdClient, PR_PRIORITY_NORMAL,
                                 PR_LOCAL_THREAD,PR_UNJOINABLE_THREAD, 0);
  if (!clientThread) {
    LOG(("NetworkTest server side: Error creating client thread"));
    LogError();
    return -1;
  }
  return 0;
}
