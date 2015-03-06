/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prerror.h"
#include "prnetdb.h"
#include "UDPserver.h"
#include "prlog.h"
#include "HelpFunctions.h"
#include "ClientSocket.h"
#include <cstring>

extern PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)
// after this short interval, we will return to PR_Poll
#define NS_SOCKET_CONNECT_TIMEOUT PR_MillisecondsToInterval(20)
#define SERVERSNDBUFFERSIZE 12582912

extern uint64_t maxBytes;

static void PR_CALLBACK
UDPSocketThread(void *_port)
{
  LOG(("NetworkTest UDP server side: A thread created."));
  uint16_t *portp = (uint16_t*)_port;
  uint16_t port = *portp;
  delete portp;
  LOG(("NetworkTest UDP server side: Init socket: port %d", port));
  PRNetAddr addr;
  PRNetAddrValue val = PR_IpAddrAny;
  PRStatus status = PR_SetNetAddr(val, PR_AF_INET, port, &addr);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return;
  }

  char host[164] = {0};
  PR_NetAddrToString(&addr, host, sizeof(host));
  LOG(("NetworkTest UDP server side: host %s", host));

  PRFileDesc *fd = PR_OpenUDPSocket(addr.raw.family);
  if (!fd) {
    LogError("UDP");
    return;
  }
  LOG(("NetworkTest UDP server side: Socket opened."));

  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(fd, &opt);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return;
  }

  opt.option = PR_SockOpt_Reuseaddr;
  opt.value.reuse_addr = true;
  status = PR_SetSocketOption(fd, &opt);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return;
  }
  LOG(("NetworkTest UDP server side: Socket options set."));

  status = PR_Bind(fd, &addr);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return;
  }

  std::vector<ClientSocket*> clients;

  PRPollDesc pollElem;
  pollElem.fd = fd;
  pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
  char buf[1500];

  int rv = 0;
  while (!rv) {
    // See if we need to send something.
    std::vector<ClientSocket*>::iterator it = clients.begin();
    while(!rv && it != clients.end()) {
      rv = (*it)->SendAcks(fd);
      if (rv < 0) {
        continue;
      }
      bool finish = false;
      rv = (*it)->MaybeSendSomethingOrCheckFinish(fd, finish);
      if (rv < 0) {
        continue;
      }
      if (finish) {
        delete (*it);
        it = clients.erase(it);
      } else {
        it++;
      }
    }
    if (rv) {
      continue;
    }
    // See if we got something.
    pollElem.out_flags = 0;
    PR_Poll(&pollElem, 1, PR_INTERVAL_NO_WAIT);
    if (pollElem.out_flags & (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL))
    {
      LOG(("NetworkTest UDP client: Closing."));
      rv = -1;
      return;
    }

    if (pollElem.out_flags & PR_POLL_READ) {
      PRNetAddr prAddr;
      int32_t count;
      count = PR_RecvFrom(fd, buf, sizeof(buf), 0, &prAddr,
                          PR_INTERVAL_NO_WAIT);

      if (count < 0) {
        PRErrorCode code = PR_GetError();
        if (code == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        rv = LogErrorWithCode(code, "UDP");
        continue;
      }
      std::vector<ClientSocket*>::iterator it = clients.begin();
      while (it != clients.end() && !(*it)->IsThisSocket(&prAddr)) {
        it++;
      }
      if (it == clients.end()) {
        clients.push_back(new ClientSocket(&prAddr));
        it = clients.end() - 1;
      }
      (*it)->NewPkt(count, buf);
    }
  }

  PR_Close(fd);
}

UDPserver::UDPserver()
  : mThreads(NULL)
  , mNumberOfPorts(0)
{
}

UDPserver::~UDPserver()
{
  for (int inx = 0; inx < mNumberOfPorts; inx++) {
    if (mThreads[inx]) {
      PR_JoinThread(mThreads[inx]);
    }
  }
}

int
UDPserver::Start(uint16_t *aPort, int aNumberOfPorts)
{
  if (!(aNumberOfPorts > 0)) {
    return -1;
  }
  mThreads = new PRThread*[aNumberOfPorts];
  for (int inx = 0; inx < aNumberOfPorts; inx++) {
    mThreads[inx] = NULL;
  }
  mNumberOfPorts = aNumberOfPorts;
  for (int inx = 0; inx < aNumberOfPorts; inx++) {
    int rv = Init(aPort[inx], inx);
    if (rv) {
      LOG(("NetworkTest server side: Error creating a thread"));
    }
  }
  return 0;
}

int
UDPserver::Init(uint16_t aPort, int aInx)
{
  uint16_t *port = new uint16_t(aPort);
  mThreads[aInx] = PR_CreateThread(PR_USER_THREAD, UDPSocketThread,
                                   (void *)port, PR_PRIORITY_NORMAL,
                                   PR_LOCAL_THREAD,PR_JOINABLE_THREAD, 0);
  if (!mThreads[aInx]) {
    LOG(("NetworkTest server side: Error creating a thread"));
    LogError("UDP");
    return -1;
  }
  return 0;
}
