#include "TCP.h"

extern PRLogModuleInfo* gClientTestLog;
#define LOG(args) PR_LOG(gClientTestLog, PR_LOG_DEBUG, args)
// after this short interval, we will return to PR_Poll
#define NS_SOCKET_CONNECT_TIMEOUT PR_MillisecondsToInterval(400)
#define SNDBUFFERSIZE 12582912

uint64_t maxBytes = 3 * (1<<22);

nsresult
ErrorAccordingToNSPR()
{
  PRErrorCode errCode = PR_GetError();
  return ErrorAccordingToNSPRWithCode(errCode);
}

nsresult
ErrorAccordingToNSPRWithCode(PRErrorCode errCode)
{
  int errLen = PR_GetErrorTextLength();
  nsAutoCString errStr;
  if (errLen > 0) {
    errStr.SetLength(errLen);
    PR_GetErrorText(errStr.BeginWriting());
  }
  LOG(("NetworkTest TCP client: Error: %x %s, %x", errCode, errStr.BeginWriting(), PR_GetOSError()));

  nsresult rv = NS_ERROR_FAILURE;
  switch (errCode) {
    case PR_WOULD_BLOCK_ERROR:
      rv = NS_BASE_STREAM_WOULD_BLOCK;
      break;
    case PR_CONNECT_ABORTED_ERROR:
    case PR_CONNECT_RESET_ERROR:
      rv = NS_ERROR_NET_RESET;
      break;
    case PR_END_OF_FILE_ERROR: // XXX document this correlation
      rv = NS_ERROR_NET_INTERRUPT;
      break;
    case PR_CONNECT_REFUSED_ERROR:
    case PR_NETWORK_UNREACHABLE_ERROR:
    case PR_HOST_UNREACHABLE_ERROR:
    case PR_ADDRESS_NOT_AVAILABLE_ERROR:
    case PR_NO_ACCESS_RIGHTS_ERROR:
      rv = NS_ERROR_CONNECTION_REFUSED;
      break;
    case PR_ADDRESS_NOT_SUPPORTED_ERROR:
      rv = NS_ERROR_SOCKET_ADDRESS_NOT_SUPPORTED;
      break;
    case PR_IO_TIMEOUT_ERROR:
    case PR_CONNECT_TIMEOUT_ERROR:
      rv = NS_ERROR_NET_TIMEOUT;
      break;
    case PR_OUT_OF_MEMORY_ERROR:
    case PR_PROC_DESC_TABLE_FULL_ERROR:
    case PR_SYS_DESC_TABLE_FULL_ERROR:
    case PR_INSUFFICIENT_RESOURCES_ERROR:
      rv = NS_ERROR_OUT_OF_MEMORY;
      break;
    case PR_ADDRESS_IN_USE_ERROR:
      rv = NS_ERROR_SOCKET_ADDRESS_IN_USE;
      break;
    case PR_FILE_NOT_FOUND_ERROR:
      rv = NS_ERROR_FILE_NOT_FOUND;
      break;
    case PR_IS_DIRECTORY_ERROR:
      rv = NS_ERROR_FILE_IS_DIRECTORY;
      break;
    case PR_LOOP_ERROR:
      rv = NS_ERROR_FILE_UNRESOLVABLE_SYMLINK;
      break;
    case PR_NAME_TOO_LONG_ERROR:
      rv = NS_ERROR_FILE_NAME_TOO_LONG;
      break;
    case PR_NO_DEVICE_SPACE_ERROR:
      rv = NS_ERROR_FILE_NO_DEVICE_SPACE;
      break;
    case PR_NOT_DIRECTORY_ERROR:
      rv = NS_ERROR_FILE_NOT_DIRECTORY;
      break;
    case PR_READ_ONLY_FILESYSTEM_ERROR:
      rv = NS_ERROR_FILE_READ_ONLY;
      break;
    default:
      break;
  }
  return rv;
}

TCP::TCP(PRNetAddr *aNetAddr)
  : mFd(nullptr)
{
  memcpy(&mNetAddr, aNetAddr, sizeof(PRNetAddr));
}

TCP::~TCP()
{
  if (mFd) {
    PR_Close(mFd);
  }
}

nsresult
TCP::Start(int aTestType)
{
  mTestType = aTestType;
  nsresult rv = Init();
  if (NS_FAILED(rv)) {
    if(mFd) {
      PR_Close(mFd);
      mFd = nullptr;
    }
    return rv;
  }

  rv = Run();  
  if (NS_FAILED(rv)) {
    if(mFd) {
      PR_Close(mFd);
      mFd = nullptr;
    }
  }
  return rv;
}

nsresult
TCP::Init()
{

  LOG(("NetworkTest TCP client: Open socket"));
  char host[164] = {0};
  PR_NetAddrToString(&mNetAddr, host, sizeof(host));
  LOG(("NetworkTest TCP client: Host: %s", host));
  LOG(("NetworkTest TCP client: AF: %d", mNetAddr.raw.family));
  int port;
  if (mNetAddr.raw.family == AF_INET) {
    port = mNetAddr.inet.port;
  } else if (mNetAddr.raw.family == AF_INET6) {
    port = mNetAddr.ipv6.port;
  }
  LOG(("NetworkTest TCP client: port: %d", port));

  mFd = PR_OpenTCPSocket(mNetAddr.raw.family);
  if (!mFd) {
    return ErrorAccordingToNSPR();
  }

  LOG(("NetworkTest TCP client: Set Options"));
  PRStatus status;
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(mFd, &opt);
  if (status != PR_SUCCESS) {
    return ErrorAccordingToNSPR();
  }

  opt.option = PR_SockOpt_NoDelay;
  opt.value.no_delay = true;
  status = PR_SetSocketOption(mFd, &opt);
  if (status != PR_SUCCESS) {
    return ErrorAccordingToNSPR();
  }

  opt.option = PR_SockOpt_SendBufferSize;
  opt.value.send_buffer_size = SNDBUFFERSIZE;
  PR_SetSocketOption(mFd, &opt);
  if (status != PR_SUCCESS) {
    return ErrorAccordingToNSPR();
  }

  LOG(("NetworkTest TCP client: Connect..."));
  status = PR_Connect(mFd, &mNetAddr, NS_SOCKET_CONNECT_TIMEOUT);
  if (status != PR_SUCCESS) {
    PRErrorCode errCode = PR_GetError();
    if (PR_IS_CONNECTED_ERROR == errCode) {
      LOG(("NetworkTest TCP client: It si connected"));
      return NS_OK;
    } else if ((PR_WOULD_BLOCK_ERROR == errCode) ||
               (PR_IN_PROGRESS_ERROR == errCode)) {
      PRPollDesc pollElem;
      pollElem.fd = mFd;
      pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
      LOG(("NetworkTest TCP client: Poll for a connection."));
      while (1) {
        pollElem.out_flags = 0;
        PR_Poll(&pollElem, 1, PR_INTERVAL_NO_WAIT);
        if ( pollElem.out_flags & PR_POLL_WRITE ) {
          LOG(("NetworkTest TCP client: Connected."));
          return NS_OK;
        } else if (pollElem.out_flags &
                   (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL)) {
          LOG(("NetworkTest TCP client: Could not connect."));
          return ErrorAccordingToNSPR();;
        }
      }
    }
    return ErrorAccordingToNSPRWithCode(errCode);
  }
  return NS_OK;
}

nsresult
TCP::Run()
{
  PRPollDesc pollElem;
  pollElem.fd = mFd;
  pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
  uint64_t readBytes = 0;
  uint64_t writtenBytes = 0;
  char buf[1500];
  switch (mTestType) {
    case 2:
      memcpy(buf, TCP_reachability, 6);
      break;
    case 3:
      memcpy(buf, TCP_performanceFromServerToClient, 6);
      break;
    default:
      return NS_ERROR_FAILURE;
  }

  LOG(("NetworkTest TCP client: Poll"));
  while (1) {
    pollElem.out_flags = 0;
    PR_Poll(&pollElem, 1, PR_INTERVAL_NO_WAIT);
    if (pollElem.out_flags & (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL)) {
      LOG(("NetworkTest TCP client: Closing: read bytes %lu", readBytes));
      PRErrorCode code = PR_GetError();
      if (code == PR_WOULD_BLOCK_ERROR) {
        continue;
      }
      return ErrorAccordingToNSPRWithCode(code);
    }
    switch (mTestType) {
      case 2:
        if (pollElem.out_flags & PR_POLL_WRITE) {
          LOG(("NetworkTest TCP client: Sending data for test - TCP reachability."));
          int write = PR_Write(mFd, buf + writtenBytes, 1500 - writtenBytes);
          if (write < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              continue;
            }
            return ErrorAccordingToNSPRWithCode(code);
          }
          
          writtenBytes += write;
          LOG(("NetworkTest TCP client: Sending data for test - "
               "TCP reachability - sent %lu bytes.", writtenBytes));
          if (writtenBytes >= 1500) {
            pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
          }
        }
        if (pollElem.out_flags & PR_POLL_READ) {
          int read = PR_Read(mFd, buf, 1500);
          if (read < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              continue;
            }
            return ErrorAccordingToNSPR();
          }
          readBytes += read;
          LOG(("NetworkTest TCP client: Receivinging data for test - "
               "TCP reachability - received %lu bytes.", writtenBytes));
          if (readBytes >= 1500) {
            LOG(("NetworkTest TCP client: Closing: read enough bytes - %lu",
                 readBytes));
            return NS_OK;
          }
      
        }
        break;

      case 3: //Sending from server to the client.
        if (pollElem.out_flags & PR_POLL_WRITE) {
          LOG(("NetworkTest TCP client: Send the first packet for test - send"
               " data from server to client with TCP."));
          int write = PR_Write(mFd, buf + writtenBytes, 1500 - writtenBytes);
          if (write < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              continue;
            }
            return ErrorAccordingToNSPRWithCode(code);
          }

          writtenBytes += write;
          LOG(("NetworkTest TCP client: Send the first packet for test - send"
               " data from server to client with TCP - sent %lu bytes.",
               writtenBytes));
          if (writtenBytes >= 1500) {
            pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
          }
        }
        if (pollElem.out_flags & PR_POLL_READ) {
          int read = PR_Read(mFd, buf, 1500);
          if (read < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              continue;
            }
            return ErrorAccordingToNSPR();
          }
          readBytes += read;
          LOG(("NetworkTest TCP client: Receivinging data for test - send"
               " data from server to client with TCP  - received %lu bytes.",
               readBytes));
          if (readBytes >= maxBytes) {
            LOG(("NetworkTest TCP client: Closing test send data from server "
                 "to client with TCP: read enough bytes - %lu",
                 readBytes));
            return NS_OK;
          }
        }
        break;

      default:
        return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}
