/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientSocket.h"
#include "prerror.h"
#include "HelpFunctions.h"
#include <math.h>
#include <cstring>
#include "prlog.h"

// I will give test a code:
#define UDP_reachability "Test_1"
#define UDP_performanceFromServerToClient "Test_5"
#define UDP_performanceFromClientToServer "Test_6"
#define TEST_prefix "Test_"
#define FINISH "Finish"

// todo this is dup'd in udp.cpp
#define RETRANSMISSION_TIMEOUT 400
#define MAX_RETRANSMISSIONS 5
#define SHUTDOWNTIMEOUT 1000
#define NOPKTTIMEOUT 2000
#define PAYLOADSIZE 1450
#define PAYLOADSIZEF ((double) PAYLOADSIZE)

/**
 *  Packet format:
 *    packet id : 4bytes
 *    timestamp: 4bytes
 *    packet type ("Type_...") or time elapse on receiver for acks.
 *    rate fore test 5.
 *
 *  Test 1:
 *   - receive a packet that start with "Test_1" (maybe we move this not to be
 *     at the beginning) (NewPkt()). If multiple packets with the same pktid are
 *     received do nothing.
 *   - try to send the response packet (MaybeSendSomething()) and repeat sending
 *     the response packet until MAX_RETRANSMISSIONS are sent, because we cannot
 *     guaranty that the other side received the packets.
 *     States: got packet -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *
 *  Test 5:
 *   - receive a packet that start with "Test_5" followed by 4 byte uint rate in
 *     packets per second mPktPerSec (NewPkt()).. If multiple packet with the
 *     same pktid are received do nothing.
 *   - send max(maxByte, maxTime) packets at rate mPktPerSec.
 *   - wait SHUTDOWNTIMEOUT time for outstanding acks.
 *     States: got a packet -> RUN_TEST
 *             RUN_TEST -> sending data -> (send enough data) -> FINISH_PACKET
 *                      -> no acks for some time -> error
 *             FINISH_PACKET -> received ack (this ack contains observed rate) -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                           -> no acks for some time -> error
 *
 *  Test 6:
 *   - receive a packet that start with "Test_6" (NewPkt()), ack the packet.
 *     If multiple packets with the same pktid are received send an ack.
 *   - for each received packet send an ack.
 *   - if we have not receive data for NOPKTTIMEOUT time finish the test and send
 *     report.(report part is not implemented)
 *   - if we receive a data packet that represent start of a new test, send the
 *     report.
 *     (maybe change this to send report when packet with "FINISH" is received - it must be reliable)
 *     States: got a packet -> RUN_TEST
 *             RUN_TEST -> receiving data ->(received FINISH) -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                      -> no packets for some time-> error
 *
 */

extern PRIntervalTime maxTime;
extern PRIntervalTime maxBytes;

int pktIdStart = 0;
int tsStart = 4; //timestamp
int typeStart = 8; //type.
int delayStart = 8; //delay.
int rateStart = 15;
int finishStart = 8;
int pktIdLen = 4;
int tsLen = 4; //timestamp
int typeLen = 7; //type.
int delayLen = 4; //delay.
int rateLen = 8;
int finishLen = 7;

extern PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)

ClientSocket::ClientSocket(PRNetAddr *aAddr)
  : mTestType(0)
  , mFirstPktSent(0)
  , mNextTimeToDoSomething(0)
  , mSentBytes(0)
  , mRecvBytes(0)
  , mNumberOfRetransFinish(0)
  , mPktPerSec(0)
  , mPktInterval(0)
  , mNextToSendInns(0)
  , mNextPktId(0)
{

  memcpy(&mNetAddr, aAddr, sizeof(PRNetAddr));
  mNodataTimeout = PR_MillisecondsToInterval(NOPKTTIMEOUT);

}

int
ClientSocket::MaybeSendSomethingOrCheckFinish(PRFileDesc *aFd,
                                              bool &aClientFinished)
{
  aClientFinished = false;
  int rv = 0;
  PRIntervalTime now = PR_IntervalNow();
  if (mNextTimeToDoSomething && mNextTimeToDoSomething < now) {
//    LOG(("NetworkTest UDP server side: Maybe send something."));
    switch (mPhase) {
      case RUN_TEST:
        rv = RunTestSend(aFd);
        break;
      case FINISH_PACKET:
        rv = SendFinishPacket(aFd);
        break;
      case WAIT_FINISH_TIMEOUT:
        rv = WaitForFinishTimeout();
        break;
      case TEST_FINISHED:
        break;
    }
    if (rv) {
      return rv;
    }
  }

  if (mLastReceivedTimeout && mLastReceivedTimeout < now) {
    rv = NoDataForTooLong();
  }

  if (mPhase == TEST_FINISHED) {
    aClientFinished = true;
  }

  return rv;
}

int
ClientSocket::RunTestSend(PRFileDesc *aFd)
{
//  LOG(("NetworkTest UDP server side: Run test send."));
  PRIntervalTime now;
  switch (mTestType) {
    case 1:
    case 6:
      break;

    case 5: //Sending from server to the client.
      {
        // Here we are sending data from server to parent until we have sent
        // maxBytes or maxTime expired. When test is finished we wait
        // SHUTDOWNTIMEOUT for outstanding acks to be received.

        now = PR_IntervalNow();
        while (mNextTimeToDoSomething < now) {
          char buf[1500];
          uint32_t id = htonl(mNextPktId);
          memcpy(buf + pktIdStart, &id, pktIdLen);
          now = PR_IntervalNow();
          memcpy(buf + tsStart, &now, tsLen);

          if ((mSentBytes >= maxBytes) &&
              (mFirstPktSent &&
               PR_IntervalToSeconds(now - mFirstPktSent) >= maxTime)) {
            LOG(("Test 5 finished: time %lu, first packet sent %lu, "
                 "duration %lu, sent %llu max to send %llu", now, mFirstPktSent,
                 PR_IntervalToSeconds(now - mFirstPktSent), mSentBytes,
                 maxBytes));
            mLastPktId = mNextPktId;
            memcpy(buf + finishStart, FINISH, finishLen);
            mPhase = FINISH_PACKET;
          }
          int count = PR_SendTo(aFd, buf, PAYLOADSIZE, 0, &mNetAddr,
                                PR_INTERVAL_NO_WAIT);
          if (count < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              return 0;
            }
            return LogErrorWithCode(code, "UDP");
          }
          mSentBytes += count;
          mNextPktId++;
          if (mFirstPktSent == 0) {
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
//          LOG(("NetworkTest UDP server: Send packet for test - send"
//               " data from a server to a client with UDP - sent %lu "
//               "bytes,  %llu.", mSentBytes, now));
        }
      }
      break;
    default:
      return -1;
  }
//  LOG(("NetworkTest UDP server side: Test %d: time %lu, next time to do "
//       "something %lu no packet timeout %lu.",
//       mTestType, now, mNextTimeToDoSomething, mLastReceivedTimeout));
  return 0;
}

int
ClientSocket::SendFinishPacket(PRFileDesc *aFd)
{
  LOG(("NetworkTest UDP server side: Send finish packet."));
  if (mNumberOfRetransFinish > MAX_RETRANSMISSIONS) {
    mError = true;
    mPhase = TEST_FINISHED;
    return 0;
  }

  char buf[1500];
  memset(buf, 0xa0, 1500);
  // Send a packet.
  uint32_t id = htonl(mLastPktId);
  memcpy(buf + pktIdStart, &id, pktIdLen);
  PRIntervalTime now = PR_IntervalNow();
  memcpy(buf + tsStart, &now, tsLen);
  memcpy(buf + finishStart, FINISH, finishLen);
  int count = PR_SendTo(aFd, buf, 1500, 0, &mNetAddr,
                        PR_INTERVAL_NO_WAIT);
  if (count < 1) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      return 0;
    }
    return LogErrorWithCode(code, "UDP");
  }
  mSentBytes += count;
  LOG(("NetworkTest UDP sever side: Sending data for test %d"
       " - sent %lu bytes.", mTestType,  mSentBytes));
  mNextTimeToDoSomething = now +
                           PR_MillisecondsToInterval(RETRANSMISSION_TIMEOUT);
  mNumberOfRetransFinish++;
  return 0;
}

int
ClientSocket::WaitForFinishTimeout()
{
  LOG(("NetworkTest UDP server side: Wait for Finish timeout."));
  mError = false;
  mPhase = TEST_FINISHED;
  return 0;
}

int
ClientSocket::NoDataForTooLong()
{
  LOG(("NetworkTest UDP server side: No data for too long."));
  mError = true;
  mPhase = TEST_FINISHED;
  return 0;
}

int
ClientSocket::SendAcks(PRFileDesc *aFd)
{
  int del = 0;
  for (std::vector<Ack>::iterator it = mAcksToSend.begin();
       it != mAcksToSend.end(); it++) {
//    LOG(("NetworkTest UDP server side: Send ack."));
    int rv = it->SendPkt(aFd, &mNetAddr);
    if (rv == PR_WOULD_BLOCK_ERROR) {
      break;
    }
    if (rv != 0) {
      return rv;
    }
    del++;
  }
  if (del) {
    mAcksToSend.erase(mAcksToSend.begin(), mAcksToSend.begin() + del);
  }
  return 0;
}

bool
ClientSocket::IsThisSocket(const PRNetAddr *aAddr)
{
  return (memcmp(&mNetAddr, aAddr, sizeof(PRNetAddr)) == 0);
}

int
ClientSocket::NewPkt(int32_t aCount, char *aBuf)
{
//  LOG(("NetworkTest UDP server side: Packet received."));
  // We can receive a data packet(test 6) or an ack(test 5) or a start of a new
  // test.
  // if we have not received packet for a long time we can assume broken
  // connection.
  PRIntervalTime lastReceived = PR_IntervalNow();
  if (memcmp(aBuf + typeStart, TEST_prefix, 5) == 0) {

    if (memcmp(mPktIdFirstPkt, aBuf + pktIdStart, pktIdLen) == 0) {
      LOG(("NetworkTest UDP server side: Received a dup of the first "
           "packet."));

      if (mTestType == 1) {
        mAcksToSend.push_back(Ack(aBuf, lastReceived, aCount, 0));
      } else if (mTestType == 5) {
        mNextTimeToDoSomething = lastReceived;
      } else if (mTestType == 6) {
        mAcksToSend.push_back(Ack(aBuf, lastReceived, 0, 0));
        mLastReceivedTimeout = lastReceived + mNodataTimeout;
      }
      return 0;
    }

    //if it is not a duplicate start a new test.

    // If the last test is in WAIT_FINISH_TIMEOUT or not finished properly send
    // or save a report.
    if (mTestType == 6) {
      //TODO: send report, or safe it for sending later
    }

    // reset
    mFirstPktSent = 0;
    mFirstPktReceived = 0;
    mNextTimeToDoSomething = 0;
    mSentBytes = 0;
    mRecvBytes = 0;
    mAcksToSend.clear();
    mNumberOfRetransFinish = 0;
    mPktPerSec = 0;
    mPktInterval = 0;
    mNextToSendInns = 0;
    memcpy(mPktIdFirstPkt, aBuf + pktIdStart, pktIdLen);
    mNextPktId = ntohl(*((uint32_t*)mPktIdFirstPkt)) + 1;
    mPktPerSecObserved = 0;
    mNextPktId = 0;
    mLastPktId = 0;

    if (memcmp(aBuf + typeStart, UDP_reachability, 6) == 0) {

      mTestType = 1;
      mPhase = WAIT_FINISH_TIMEOUT;
      // Send a reply.
      mAcksToSend.push_back(Ack(aBuf, lastReceived, aCount, 0));
      mNextTimeToDoSomething = lastReceived +
                               PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
      LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));

    } else if (memcmp(aBuf + typeStart, UDP_performanceFromServerToClient,
                      6) == 0) {

      mNextTimeToDoSomething = lastReceived;
      mTestType = 5;
      LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));
      mRecvBytes +=aCount;
      uint32_t npktpersec;
      memcpy(&npktpersec, aBuf + rateStart, rateLen);
      mPktPerSec = ntohl(npktpersec);
      if (mPktPerSec == 0) {
        mError = true;
        mPhase = TEST_FINISHED;
        return 0;
      }

      mPktInterval = 1000000000 / mPktPerSec; // the interval in ns.
      mPhase = RUN_TEST;
      LOG(("NetworkTest UDP server side: Test %d: rate %d interval %lf.",
           mTestType, mPktPerSec, mPktInterval));
      mLastReceivedTimeout = lastReceived + mNodataTimeout;
    } else if (memcmp(aBuf + typeStart, UDP_performanceFromClientToServer, 6) == 0) {

      mAcksToSend.push_back(Ack(aBuf, lastReceived, 0, 0));
      mLastReceivedTimeout = lastReceived + mNodataTimeout;
      mFirstPktReceived = lastReceived;
      mTestType = 6;
      LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));
      mPhase = RUN_TEST;
    } else {
      LOG(("NetworkTest UDP server side: Test not implemented"));
     return -1;
    }
  } else {
    if (mTestType == 0) {
      mError = true;
      mPhase = TEST_FINISHED;
      return 0;
    }

    switch (mTestType) {
      case 5:
        // Recv an ack. TODO add data collecting.
        mRecvBytes +=aCount;
//        LOG(("NetworkTest UDP server side: Receiving data for test - send data "
//             "from a server to a client with UDP  - received ACK received "
//             "%lu bytes.", mRecvBytes));

        if (mPhase == FINISH_PACKET) {
          uint32_t id;
          memcpy(&id, aBuf + pktIdStart, pktIdLen);
          if (mLastPktId == ntohl(id)) {
            mPhase = WAIT_FINISH_TIMEOUT;
            mNextTimeToDoSomething = lastReceived +
                                     PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
          }
        }

        mLastReceivedTimeout = lastReceived + mNodataTimeout;
        break;
      case 6:
        mRecvBytes +=aCount;
//        LOG(("NetworkTest UDP server side: Receivinging data for test - send"
//             " data from a client to a server with UDP  - received %lu "
//             "bytes.", mRecvBytes));

        if (mPhase == RUN_TEST) {
          if (memcmp(aBuf + finishStart, FINISH, finishLen) == 0) {
            mPhase = WAIT_FINISH_TIMEOUT;
            mNextTimeToDoSomething = lastReceived +
                                     PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
            if (!mPktPerSecObserved &&
                PR_IntervalToSeconds(PR_IntervalNow() - mFirstPktReceived)) {
              mPktPerSecObserved = (double)mRecvBytes / PAYLOADSIZEF /
                (double)PR_IntervalToMilliseconds(PR_IntervalNow() - mFirstPktReceived)
                * 1000.0;
            }
            LOG(("NetworkTest UDP client: Closing, observed rate: %lu",
                  mPktPerSecObserved));
            LOG(("Test 6 finished: time %lu, first packet sent %lu, "
                 "duration %lu, received %llu max to received %llu.",
                 PR_IntervalNow(),
                 mFirstPktReceived,
                 PR_IntervalToMilliseconds(PR_IntervalNow() - mFirstPktReceived),
                 mRecvBytes, maxBytes));
          }
        }

        // Send ack.
        mAcksToSend.push_back(Ack(aBuf, lastReceived, 0,
                                  mPktPerSecObserved));

        mLastReceivedTimeout = lastReceived + mNodataTimeout;
      break;
    default:
      return -1;
    }
  }
//  LOG(("NetworkTest UDP server side: Test %d: time %lu next time to do "
//       "something %lu no packet timeout %lu.",
//       mTestType, lastReceived, mNextTimeToDoSomething, mLastReceivedTimeout));
  return 0;
}
