/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UDP.h"
#include "prerror.h"
#include "HelpFunctions.h"
#include <math.h>
#include <cstring>
#include "prlog.h"
#include "prrng.h"

// I will give test a code:
#define UDP_reachability "Test_1"
#define UDP_performanceFromServerToClient "Test_5"
#define UDP_performanceFromClientToServer "Test_6"
#define FINISH "Finish"
#define TEST_prefix "Test_"

#define RETRANSMISSION_TIMEOUT 200
#define MAX_RETRANSMISSIONS 10
#define SHUTDOWNTIMEOUT 1000
#define NOPKTTIMEOUT 2000

/**
 *  Packet format:
 *    packet id : 4bytes
 *    timestamp: 4bytes
 *    packet type ("Type_...") or time elapse on receiver for acks.
 *    rate fore test 5.
 *
 *  Test 1:
 *   - send a packet that starts with "pkdID,ts,Test_1" (maybe we move this not
 *     to be at the beginning). Wait for an ack. If no ack is received after
 *     RETRANSMISSION_TIMEOUT, send another packet with the same pktID but
 *     a different timestamp.
 *     States: START_TEST -> get ack -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                        -> no ack -> error
 *
 *  Test 5:
 *   - send a packet that starts with "pkdID,ts,Test_5" followed by 4 byte int
 *     rate in packets per second mPktPerSec. If no packet is received after
 *     RETRANSMISSION_TIMEOUT send another packet with the same pktID but
 *     different timestamp.
 *   - If no packet is received from the server after MAX_RETRANSMISSIONS return
 *     NS_ERROR_FAILURE.
 *   - Receive and ack packets coming from the server.If no packet is received
 *     for NOPKTTIMEOUT seconds close the connection and return NS_OK.
 *     States: START_TEST -> get ack -> RUN_TEST
 *                        -> no ack -> error
 *             RUN_TEST -> receiving data ->(received FINISH) -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                      -> no packets for some time-> error
 *
 *  Test 6:
 *   - send a packet that start with "pkdID,ts,Test_6". If no packet is received
 *     after RETRANSMISSION_TIMEOUT send another packet with the same pktID but
 *     different timestamp.
 *   - If no packet is received from the server after MAX_RETRANSMISSIONS return
 *     NS_ERROR_FAILURE.
 *   - Send packet at rate mPktPerSec and receive ack. After max(maxBytes, maxTime)
 *     packets are sent, stop sending data and wait for anouther SHUTDOWNTIMEOUT
 *     seconds for incoming acks.*
 *     States: START_TEST -> get ack -> RUN_TEST
 *                        -> no ack -> error
 *             RUN_TEST -> sending data -> (send enough data) -> FINISH_PACKET
 *                      -> no acks for some time -> error
 *             FINISH_PACKET -> received ack (this ack contains observed rate) -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                           -> no acks for some time -> error
 *
 */

namespace NetworkPath {

extern uint32_t maxTime;
extern uint64_t maxBytes;

int pktIdStart = 0;
int tsStart = 4; //timestamp
int typeStart = 8; //type.
int delayStart = 8; //delay.
int rateStart = 15;
int pktIdLen = 4;
int tsLen = 4; //timestamp
int typeLen = 7; //type.
int delayLen = 4; //delay.
int rateLen = 8;
int finishStart = 8;
int finishLen = 7;

extern PRLogModuleInfo* gClientTestLog;
#define LOG(args) PR_LOG(gClientTestLog, PR_LOG_DEBUG, args)

UDP::UDP(PRNetAddr *aAddr, uint16_t aLocalPort)
  : mFd(nullptr)
  , mTestType(0)
  , mLastReceivedTimeout(0)
  , mNextTimeToDoSomething(0)
  , mSentBytes(0)
  , mRecvBytes(0)
  , mNumberOfRetrans(0)
  , mPktPerSec(0)
  , mPktInterval(0)
  , mNextPktId(0)
  , mLastPktId(0)
  , mError(false)
{

  memcpy(&mNetAddr, aAddr, sizeof(PRNetAddr));
  mLocalPort = aLocalPort;
  mNodataTimeout = PR_MillisecondsToInterval(NOPKTTIMEOUT);

}

UDP::~UDP()
{
  if (mFd) {
    PR_Close(mFd);
  }
}

nsresult
UDP::Start(int aTestType, uint32_t aRate, bool &aSucceeded)
{
  aSucceeded = false;
  mTestType = aTestType;
  mRate = aRate;
  if (mRate) {
    mPktInterval = 1000000000 / mRate; // in nanosecond.
    LOG(("NetworkTest UDP client side: Test %d: rate %d interval %lf.",
         mTestType, mRate, mPktInterval));
  }
  nsresult rv = Init();
  if (NS_FAILED(rv)) {
    if(mFd) {
      PR_Close(mFd);
      mFd = nullptr;
    }
    return rv;
  }

  rv = Run();
  if(mFd) {
    PR_Close(mFd);
    mFd = nullptr;
  }
  if (NS_SUCCEEDED(rv) && !mError) {
    aSucceeded = true;
  }
  return rv;
}

nsresult
UDP::Init()
{

  LOG(("NetworkTest UDP client: Open socket"));
  char host[164] = {0};
  PR_NetAddrToString(&mNetAddr, host, sizeof(host));
  LOG(("NetworkTest UDP client: Remote Host: %s", host));
  LOG(("NetworkTest UDP client: AF: %d", mNetAddr.raw.family));
  int port = 0;
  if (mNetAddr.raw.family == AF_INET) {
    port = mNetAddr.inet.port;
  } else if (mNetAddr.raw.family == AF_INET6) {
    port = mNetAddr.ipv6.port;
  }
  LOG(("NetworkTest UDP client: Remote port: %d", port));

  PRNetAddr addr;
  PRNetAddrValue val = PR_IpAddrAny;
  PRStatus status = PR_SetNetAddr(val, mNetAddr.raw.family, mLocalPort, &addr);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return ErrorAccordingToNSPR("UDP");
  }

  mFd = PR_OpenUDPSocket(addr.raw.family);
  if (!mFd) {
    return ErrorAccordingToNSPR("UDP");
  }

  LOG(("NetworkTest UDP client: Set Options"));
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(mFd, &opt);
  if (status != PR_SUCCESS) {
    return ErrorAccordingToNSPR("UDP");
  }

  opt.option = PR_SockOpt_Reuseaddr;
  opt.value.reuse_addr = true;
  status = PR_SetSocketOption(mFd, &opt);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return ErrorAccordingToNSPR("UDP");
  }
  LOG(("NetworkTest UDP client: Socket options set."));

  status = PR_Bind(mFd, &addr);
  if (status != PR_SUCCESS) {
    LogError("UDP");
    return ErrorAccordingToNSPR("UDP");
  }

  return NS_OK;
}

nsresult
UDP::Run()
{
  LOG(("NetworkTest UDP client: Run."));

  mRateObserved = 0;
  mLastReceivedTimeout = 0;
  mNextTimeToDoSomething = PR_IntervalNow();
  mSentBytes = mRecvBytes = 0;
  mAcksToSend.clear();
  mNumberOfRetrans = 0;
  mNumberOfRetransFinish = 0;
  mPktPerSec = 0;
  mNextToSendInns = 0;
  memset(mPktIdFirstPkt, 0, sizeof(mPktIdFirstPkt));

  PR_GetRandomNoise(&mNextPktId, sizeof(mNextPktId));
  while (mNextPktId == 0) {
    PR_GetRandomNoise(&mNextPktId, sizeof(mNextPktId));
  }
  mLastPktId = 0;
  mFirstPktSent = 0;
  mFirstPktReceived = 0;
  mError = false;

  PRPollDesc pollElem;
  pollElem.fd = mFd;
  pollElem.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;

  mPhase = START_TEST;
  char buf[1500];
  

  nsresult rv = NS_OK;
  while (NS_SUCCEEDED(rv)) {

    // See if we need to send something.
    PRIntervalTime now = PR_IntervalNow();
    if (mNextTimeToDoSomething && mNextTimeToDoSomething < now) {
//      LOG(("NetworkTest UDP client: Iteration."));
      nsresult rv = NS_OK;
      switch (mPhase) {
        case START_TEST:
          rv = StartTestSend();
          break;
        case RUN_TEST:
          rv = RunTestSend();
          break;
        case FINISH_PACKET:
          rv = SendFinishPacket();
          break;
        case WAIT_FINISH_TIMEOUT:
          rv = WaitForFinishTimeout();
          break;
        case TEST_FINISHED:
          break;
      }
      if (NS_FAILED(rv)) {
        continue;
      }
    }

    if (mLastReceivedTimeout && mLastReceivedTimeout < now) {
      LOG(("NetworkTest UDP client: Last received timed out."));
      rv = NoDataForTooLong();
    }

    if (mPhase == TEST_FINISHED) {
      LOG(("NetworkTest UDP client: Test finished."));
      return NS_OK;
    }

    SendAcks();

    // See if we got something.
    pollElem.out_flags = 0;
    PR_Poll(&pollElem, 1, PR_INTERVAL_NO_WAIT);
    if (pollElem.out_flags & (PR_POLL_ERR | PR_POLL_HUP | PR_POLL_NVAL))
    {
      LOG(("NetworkTest UDP client: Closing: read bytes %lu send bytes %lu",
           mSentBytes, mRecvBytes));
      rv = NS_ERROR_FAILURE;
      continue;
    }

    if (pollElem.out_flags & PR_POLL_READ) {
      PRNetAddr prAddr;
      int32_t count;
      count = PR_RecvFrom(mFd, buf, sizeof(buf), 0, &prAddr,
                          PR_INTERVAL_NO_WAIT);

      if (count < 0) {
        PRErrorCode code = PR_GetError();
        if (code == PR_WOULD_BLOCK_ERROR) {
          continue;
        }
        rv = ErrorAccordingToNSPRWithCode(code, "UDP");
        continue;
      }
      rv = NewPkt(count, buf);
    }
  }

  PR_Close(mFd);
  mFd = nullptr;
  return rv;
}

nsresult
UDP::StartTestSend()
{
  char buf[1500];

  LOG(("NetworkTest UDP client: retransmissions: %d", mNumberOfRetrans));
  if (mNumberOfRetrans > MAX_RETRANSMISSIONS) {
    mError = true;
    mPhase = PHASE::TEST_FINISHED;
    return NS_OK;
  }

  // Send a packet.
  uint32_t id = htonl(mNextPktId);
  memcpy(buf + pktIdStart, &id, pktIdLen);
  PRIntervalTime now = PR_IntervalNow();
  memcpy(buf + tsStart, &now, tsLen);
  memcpy(buf + typeStart,
         (mTestType == 1) ? UDP_reachability :
         (mTestType == 5) ? UDP_performanceFromServerToClient :
         UDP_performanceFromClientToServer, typeLen);

  if (mTestType == 5) {
    uint32_t rate = htonl(mRate);
    memcpy(buf + rateStart, &rate, rateLen);
  }
  int count = PR_SendTo(mFd, buf, 1500, 0, &mNetAddr,
                        PR_INTERVAL_NO_WAIT);
  if (count < 1) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      return NS_OK;
    }
    return ErrorAccordingToNSPRWithCode(code, "UDP");
  }

//  mSentBytes += count;
  LOG(("NetworkTest UDP client: Sending data for test %d"
       " - sent %lu bytes.", mTestType,  mSentBytes));
  mNextTimeToDoSomething = now +
                           PR_MillisecondsToInterval(RETRANSMISSION_TIMEOUT);
  mNumberOfRetrans++;

  return NS_OK;
}

nsresult
UDP::RunTestSend()
{
//  LOG(("NetworkTest UDP client: Run test."));
  PRIntervalTime now;
  switch (mTestType) {
    case 1:
    case 5:
      return NS_ERROR_UNEXPECTED;
    case 6:
      // Here we are sending data from the client to the server until we have
      // sent maxBytes or maxTime expired. When test is finished we wait
      // SHUTDOWNTIMEOUT for outstanding acks to be received.
      {
        //LOG(("NetworkTest UDP client: Sending packets test 6."));
        now = PR_IntervalNow();
        while (mNextTimeToDoSomething < now) {
          char buf[1500];
          PR_GetRandomNoise(&buf, sizeof(buf));
          uint32_t id = htonl(mNextPktId);
          memcpy(buf + pktIdStart, &id, pktIdLen);
          now = PR_IntervalNow();
          memcpy(buf + tsStart, &now, tsLen);
          if ((mSentBytes >= maxBytes) && mFirstPktSent &&
              (PR_IntervalToSeconds(now - mFirstPktSent) >= maxTime)) {
            LOG(("Test 6 finished: time %lu, first packet sent %lu, "
                 "duration %lu, sent %llu max to send %llu", now, mFirstPktSent,
                 PR_IntervalToSeconds(now - mFirstPktSent), mSentBytes,
                 maxBytes));
            mLastPktId = mNextPktId;
            memcpy(buf + finishStart, FINISH, finishLen);
            mPhase = FINISH_PACKET;
          }
          int count = PR_SendTo(mFd, buf, 1500, 0, &mNetAddr,
                                PR_INTERVAL_NO_WAIT);
          if (count < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              return NS_OK;
            }
            return ErrorAccordingToNSPRWithCode(code, "UDP");
          }
          mSentBytes += count;
          mNextPktId++;
          if (!mFirstPktSent) {
            mFirstPktSent = now;
          }
          if (mPhase == FINISH_PACKET) {
            mNextTimeToDoSomething = now +
              PR_MillisecondsToInterval(RETRANSMISSION_TIMEOUT);
          } else {
            mNextToSendInns += mPktInterval;
            mNextTimeToDoSomething = mFirstPktSent +
              PR_MicrosecondsToInterval(floor(mNextToSendInns / 1000.0));
          }
          now = PR_IntervalNow();
        }
      }
      break;
    default:
      return NS_ERROR_FAILURE;
  }
//  LOG(("NetworkTest UDP client side: Test %d sent data: time %lu next time to"
//       " do something %lu no packet timeout %lu.",
//       mTestType, now, mNextTimeToDoSomething, mLastReceivedTimeout));
  return NS_OK;
}

nsresult
UDP::SendFinishPacket()
{
  LOG(("NetworkTest UDP client: Sending finish packet retrans: %d.",
       mNumberOfRetransFinish));
  if (mNumberOfRetransFinish > MAX_RETRANSMISSIONS) {
    mError = true;
    mPhase = PHASE::TEST_FINISHED;
    return NS_OK;
  }

  char buf[1500];
  // Send a packet.
  uint32_t id = htonl(mLastPktId);
  memcpy(buf + pktIdStart, &id, pktIdLen);
  PRIntervalTime now = PR_IntervalNow();
  memcpy(buf + tsStart, &now, tsLen);
  memcpy(buf + finishStart, FINISH, finishLen);
  int count = PR_SendTo(mFd, buf, 1500, 0, &mNetAddr,
                        PR_INTERVAL_NO_WAIT);
  if (count < 1) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      return NS_OK;
  }
    return ErrorAccordingToNSPRWithCode(code, "UDP");
  }
  mSentBytes += count;
  LOG(("NetworkTest UDP client: Sending data for test %d"
       " - sent %lu bytes.", mTestType,  mSentBytes));
  mNextTimeToDoSomething = now +
                           PR_MillisecondsToInterval(RETRANSMISSION_TIMEOUT);
  mNumberOfRetransFinish++;
  return NS_OK;
}

nsresult
UDP::NoDataForTooLong()
{
  LOG(("NetworkTest UDP client: oData for too long."));
  mError = true;
  mPhase = PHASE::TEST_FINISHED;
  return NS_OK;
}

nsresult
UDP::SendAcks()
{
  int del = 0;
  for (std::vector<Ack>::iterator it = mAcksToSend.begin();
       it != mAcksToSend.end(); it++) {
    int rv = it->SendPkt(mFd, &mNetAddr);
    if (rv == PR_WOULD_BLOCK_ERROR) {
      break;
    }
    if (rv != 0) {
      return ErrorAccordingToNSPRWithCode(rv, "UDP");;
    }
    del++;
  }
  if (del) {
    mAcksToSend.erase(mAcksToSend.begin(), mAcksToSend.begin() + del);
  }
  return NS_OK;
}

nsresult
UDP::NewPkt(int32_t aCount, char *aBuf)
{
  //LOG(("NetworkTest UDP client: Packet received."));
  // We can receive a data packet(test 6) or an ack(test 5) or a start of a new
  // test.
  // if we have not received packet for a long time we can assume broken
  // connection.
  PRIntervalTime lastReceived = PR_IntervalNow();

  switch (mTestType) {
    case 1:
      LOG(("NetworkTest UDP client: Receiving data for test - UDP "
           "reachability  - received %u bytes.", aCount));
      if (mPhase == PHASE::START_TEST) {
        uint32_t id;
        memcpy(&id, aBuf + pktIdStart, pktIdLen);
        if (mNextPktId != ntohl(id)) {
          LOG(("NetworkTest UDP client: recv %d should be %d", mNextPktId,
               ntohl(id)));
          return NS_OK;
        }
        mPhase = PHASE::WAIT_FINISH_TIMEOUT;
        mNextTimeToDoSomething = PR_IntervalNow() +
                                 PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
      }
      break;
    case 5:
      mRecvBytes +=aCount;
//      LOG(("NetworkTest UDP client: Receivinging data for test - send"
//           " data from server to client with UDP  - received %lu bytes.",
//           mRecvBytes));

      if (mPhase == PHASE::START_TEST) {
        mPhase = PHASE::RUN_TEST;
        mNextTimeToDoSomething = 0;
        mFirstPktReceived = lastReceived;
      }

      // Send ack.
      mAcksToSend.push_back(Ack(aBuf, lastReceived));

      if (mPhase == PHASE::RUN_TEST) {
        if (memcmp(aBuf + finishStart, FINISH, finishLen) == 0) {
          mPhase = WAIT_FINISH_TIMEOUT;
          mNextTimeToDoSomething = lastReceived +
                                   PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);

          if (PR_IntervalToSeconds(PR_IntervalNow() - mFirstPktReceived)) {
            mRateObserved = (double)mRecvBytes / 1500.0 /
              (double)PR_IntervalToMilliseconds(PR_IntervalNow() - mFirstPktReceived)
              * 1000.0;
          }
        }
      }
      mLastReceivedTimeout = lastReceived + mNodataTimeout;
      break;
    case 6:
      // Recv an ack. TODO add data collecting.
      mRecvBytes +=aCount;
//      LOG(("NetworkTest UDP client: Receiving data for test - send data from"
//           " server to client with UDP  - received ACK received %lu bytes.",
///           mRecvBytes));
      if (mPhase == PHASE::START_TEST) {
        mPhase = PHASE::RUN_TEST;
      }
      if (mPhase == PHASE::FINISH_PACKET) {
        uint32_t id;
        memcpy(&id, aBuf + pktIdStart, pktIdLen);
        if (mLastPktId == ntohl(id)) {
          mPhase = WAIT_FINISH_TIMEOUT;
          uint32_t rate;
          memcpy(&rate, aBuf + rateStart, rateLen);
          mRateObserved = ntohl(rate);
          mNextTimeToDoSomething = lastReceived +
                                   PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
        }
      }
      mLastReceivedTimeout =  lastReceived + mNodataTimeout;
     break;

    default:
      return NS_ERROR_UNEXPECTED;
  }
//  LOG(("NetworkTest UDP client side: Test %d: time %lu next time to do "
//       "something %lu no packet timeout %lu.",
//       mTestType, lastReceived, mNextTimeToDoSomething, mLastReceivedTimeout));
  return NS_OK;
}

nsresult
UDP::WaitForFinishTimeout()
{
  mError = false;
  mPhase = TEST_FINISHED;
  return NS_OK;
}

} // namespace NetworkPath
