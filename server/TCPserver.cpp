/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prerror.h"
#include "prnetdb.h"
#include "TCPserver.h"
#include "HelpFunctions.h"
#include "prlog.h"
#include "prthread.h"
#include "prmem.h"
#include "prrng.h"
#include "config.h"
#include <cstring>
#include <stdio.h>

extern PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)
// after this short interval, we will return to PR_Poll
#define NS_SOCKET_CONNECT_TIMEOUT PR_MillisecondsToInterval(20)
#define SERVERSNDBUFFERSIZE 12582912

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

void
LogLogFormat(PRFileDesc * aFile)
{
  char line1[] = "Data pkt has been recevied: [timestamp pkt received] RECV [bytes received]\n";
  PR_Write(aFile, line1, strlen(line1));

  char line2[] = "The last packet has been sent: [timestamp pkt sent] RECV [bytes sent]\n";
  PR_Write(aFile, line2, strlen(line2));
}

static void PR_CALLBACK
ClientThread(void *_fd)
{
  LOG(("NetworkTest TCP server side: Client thread created."));
  PRFileDesc *fd = (PRFileDesc*)_fd;

  PRPollDesc pollElem;
  pollElem.fd = fd;
  pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
  uint64_t writtenBytes = 0;
  int testType = 0;
  uint64_t readBytes = 0;
  uint64_t recvBytesForRate = 0;
  uint32_t  bufLen = PAYLOADSIZE;
  char buf[bufLen];
  PR_GetRandomNoise(&buf, sizeof(buf));
  PRIntervalTime timeFirstPktReceived = 0;
  PRIntervalTime startRateCalc = 0;
  uint64_t pktPerSec = 0;
  PRFileDesc *logFile;
  char logstr[80];

  while (1) {
    pollElem.out_flags = 0;
    int rv = PR_Poll(&pollElem, 1, 1000);
    if (rv < 0) {
      LogError("TCP");
      LOG(("NetworkTest TCP server side: Poll error [fd=%p]. Sent %lu bytes, "
           "received %lu bytes", fd, writtenBytes, readBytes));
      break;
    } else  if (rv == 0) {
      LOG(("NetworkTest TCP server side: Poll timeout [fd=%p]. Sent %lu bytes, "
           "received %lu bytes", fd, writtenBytes, readBytes));
      break;
    }
    if (pollElem.out_flags & (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL)) {
      PRErrorCode errCode = PR_GetError();
      LogErrorWithCode(errCode, "TCP");
      LOG(("NetworkTest TCP server side: Connection error [fd=%p]. Sent %lu "
           "bytes, received %lu bytes", fd, writtenBytes, readBytes));
      break;
    }

    if (pollElem.out_flags & PR_POLL_READ) {
      int read;
      if (readBytes < bufLen) {
        // We are reading the whole first packet.
        read = PR_Read(fd, buf + readBytes, bufLen - readBytes);
      } else {
        read = PR_Read(fd, buf, bufLen);
      }

      if (read < 1) {
        PRErrorCode errCode = PR_GetError();
        if (errCode == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        LogErrorWithCode(errCode, "TCP");
        break;
      }

      readBytes += read;
//      LOG(("NetworkTest TCP client: time %lu Test %d - received %lu bytes.",
//           PR_IntervalNow() - timeFirstPktReceived, testType, readBytes));

      // Wait to get the complete first packet.
      if (readBytes < bufLen) {
        continue;
      }
      switch (testType) {
        case 0:
          if (memcmp(buf, TCP_reachability, 6) == 0) {
            testType = 2;
            pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
          } else if (memcmp(buf, TCP_performanceFromServerToClient, 6) == 0) {
            testType = 3;
            // Sending data.
            pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
          }  else if (memcmp(buf, TCP_performanceFromClientToServer, 6) == 0) {
            testType = 4;

            // Get file name.
            char fileName[FILE_NAME_LEN];
            memcpy(fileName, buf + FILE_NAME_START, FILE_NAME_LEN);
            LOG(("File name: %s", fileName));
            logFile = OpenTmpFileForDataCollection(fileName);
            LogLogFormat(logFile);
            if (logFile) {
              sprintf(logstr, "%lu START TEST 4 %lu\n",
                      (unsigned long)PR_IntervalToMilliseconds(PR_IntervalNow()),
                      (unsigned long)readBytes);
              PR_Write(logFile, logstr, strlen(logstr));
            }

            // Receive data.
            pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;

          } else {
            LOG(("NetworkTest TCP server side: Test not implemented"));
            break;
          }
          if (!timeFirstPktReceived) {
            timeFirstPktReceived = PR_IntervalNow();
          }
          LOG(("NetworkTest TCP server side: Starting test %d.", testType));
          break;

        case 2:
        case 3:
          LOG(("NetworkTest TCP server side: We should not receive any more "
               "data in test %d.", testType));
          break;
        case 4:
          // Log data.
          if (logFile) {
            sprintf(logstr, "%lu RECV %lu\n",
                    (unsigned long)PR_IntervalToMilliseconds(PR_IntervalNow()),
                    (unsigned long)read);
            PR_Write(logFile, logstr, strlen(logstr));
          }

          if (PR_IntervalToSeconds(PR_IntervalNow() - timeFirstPktReceived) >=
              2) {
            recvBytesForRate += read;
            if (!startRateCalc) {
              startRateCalc = PR_IntervalNow();
            }
          }
          if ((readBytes >= MAXBYTES) &&
              (PR_IntervalToSeconds(PR_IntervalNow() - timeFirstPktReceived) >=
               4)) {
            uint64_t rate;
            if (PR_IntervalToSeconds(PR_IntervalNow() - startRateCalc)) {
              rate = (double)recvBytesForRate / PAYLOADSIZEF /
                (double)PR_IntervalToMilliseconds(PR_IntervalNow() - startRateCalc) *
                1000.0;
            }
            LOG(("NetworkTest TCP server side: Test 4 should treminate - "
                 "we have received enough data. Rate: %lu", rate));
            pktPerSec = htonll(rate);
            pollElem.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
            LOG(("Test 4 finished: time %lu, first packet sent %lu, "
                 "duration %lu, received %llu max to received %llu, received "
                 "bytes for rate calc %llu, duration for calc %lu",
                 PR_IntervalNow(),
                 timeFirstPktReceived,
                 PR_IntervalToMilliseconds(PR_IntervalNow() - timeFirstPktReceived),
                 readBytes, MAXBYTES, recvBytesForRate,
                 PR_IntervalNow() - startRateCalc));
          }
          break;
        default:
          break;
      }
    } else  if (pollElem.out_flags & PR_POLL_WRITE) {

      if (testType == 4) {
        PR_STATIC_ASSERT(sizeof(pktPerSec) == 8);
        memcpy(buf, &pktPerSec, sizeof(pktPerSec));
      }

      int written;
      if (testType == 3) {
        written = PR_Write(fd, buf, bufLen);
      } else {
        written = PR_Write(fd, buf + writtenBytes,
                           bufLen -writtenBytes);
      }
      if (written < 0) {
        PRErrorCode errCode = PR_GetError();
        if (errCode == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        LogErrorWithCode(errCode, "TCP");
        break;
      }
      writtenBytes += written;
      if ((testType == 2 || testType == 4) && (writtenBytes >= bufLen)) {
        pollElem.in_flags = PR_POLL_EXCEPT;
      }
    }
  }

  if (fd) {
    PR_Close(fd);
  }

  if (logFile) {
    PR_Close(logFile);
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
  LOG(("NetworkTest TCP server side: Init socket: port %d", aPort));
  PRNetAddr addr;
  PRNetAddrValue val = PR_IpAddrAny;
  PRStatus status = PR_SetNetAddr(val, PR_AF_INET, aPort, &addr);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  char host[164] = {0};
  PR_NetAddrToString(&addr, host, sizeof(host));

  LOG(("NetworkTest TCP server side: host %s", host));
  mFds[aInx] = PR_OpenTCPSocket(addr.raw.family);
  if (!mFds[aInx]) {
    LogError("TCP");
    return -1;
  }
  LOG(("NetworkTest TCP server side: Socket opened."));

  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  opt.option = PR_SockOpt_Reuseaddr;
  opt.value.reuse_addr = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  opt.option = PR_SockOpt_NoDelay;
  opt.value.no_delay = true;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  opt.option = PR_SockOpt_SendBufferSize;
  opt.value.send_buffer_size = SERVERSNDBUFFERSIZE;
  status = PR_SetSocketOption(mFds[aInx], &opt);
  if (status != PR_SUCCESS) {
    LogError("TCP");
  //  return -1;
  }

  LOG(("NetworkTest TCP server side: Socket options set."));

  status = PR_Bind(mFds[aInx], &addr);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  LOG(("NetworkTest TCP server side: Socket bind."));

  status = PR_Listen(mFds[aInx], 10);
  if (status != PR_SUCCESS) {
    LogError("TCP");
    return -1;
  }

  LOG(("NetworkTest TCP server side: Socket listens."));
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
        LOG(("NetworkTest TCP server side: Client accepted [fd=%p].", fdClient));
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
    LOG(("NetworkTest TCP server side: Error creating client thread"));
    LogError("TCP");
    return -1;
  }
  return 0;
}
