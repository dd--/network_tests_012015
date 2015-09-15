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
#include <stdio.h>
#include "prlog.h"
#include "prrng.h"

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

/**
 *  Packet formats are described in the config.h file.
 *
 *  Test 1:
 *   - receive a packet that start with "pkdID,ts,Test_1" (maybe we move this
 *     not to be at the beginning) (NewPkt()). If multiple packets with the same
 *     pktid are received just send an ack.
 *   - send a response packet
 *   - if multiple packets with the same pktid are received just send a response
 *     packet.
 *     States: got packet -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *
 *  Test 5:
 *   - receive a packet that start with "pkdID,ts,Test_5" followed by 8 byte
 *     uint64_t rate in packets per second mPktPerSec (NewPkt()). If multiple
 *     packet with the same pktid are received do nothing.
 *   - send max(MAXBYTES, MAXTIME) packets at rate mPktPerSec and receive acks.
 *   - wait SHUTDOWNTIMEOUT time for outstanding acks.
 *     States: got a packet -> RUN_TEST
 *             RUN_TEST -> sending data -> (send enough data) -> FINISH_PACKET
 *                      -> no acks for some time -> error
 *             FINISH_PACKET -> received ack -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                           -> no acks for RETRANSMISSION_TIMEOUT retransmit FINISH_PACKET
 *                           -> no acks for MAX_RETRANSMISSIONS -> error
 *
 *  Test 6:
 *   - receive a packet that start with "pkdID,ts,Test_6" (NewPkt()), ack the
 *     packet. If multiple packets with the same pktid are received send an ack.
 *   - for each other received packet send an ack.
 *   - if we have not receive data for NOPKTTIMEOUT time finish the test and
 *     close the report (log file).
 *   - if we receive a data packet that represent start of a new test, close the
 *     report.
 *     States: got a packet -> RUN_TEST
 *             RUN_TEST -> receiving data and send ack ->(received FINISH) -> ack FINISH packet -> WAIT_FINISH_TIMEOUT -> TEST_FINISHED
 *                      -> no packets for some time-> error
 *
 */

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
  , mPhase(START_TEST)
{
  memcpy(&mNetAddr, aAddr, sizeof(PRNetAddr));
  mNodataTimeout = PR_MillisecondsToInterval(NOPKTTIMEOUT);
  PR_GetRandomNoise(&mSendBuf, sizeof(mSendBuf));
  memset(mPktIdFirstPkt, '\0', PKT_ID_LEN);
}

ClientSocket::~ClientSocket()
{
  mLogFile.Done();
}
int
ClientSocket::MaybeSendSomethingOrCheckFinish(PRFileDesc *aFd,
                                              bool &aClientFinished)
{
  aClientFinished = false;
  int rv = 0;
  PRIntervalTime now = PR_IntervalNow();
  if (mNextTimeToDoSomething && mNextTimeToDoSomething < now) {
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
      case START_TEST:
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
    mLogFile.Done();
    aClientFinished = true;
  }
  return rv;
}

int
ClientSocket::RunTestSend(PRFileDesc *aFd)
{
  PRIntervalTime now;
  switch (mTestType) {
    case 1:
    case 6:
      break;

    case 5: //Sending from server to the client.
      {
        // Here we are sending data from server to the client until we have sent
        // MAXBYTES or MAXTIME has expired. When test is finished we wait
        // SHUTDOWNTIMEOUT for outstanding acks to be received.

        now = PR_IntervalNow();
        while (mNextTimeToDoSomething < now) {
          now = PR_IntervalNow();
          FormatDataPkt(PR_IntervalToMilliseconds(now));

          if ((mSentBytes >= MAXBYTES) &&
              (mFirstPktSent &&
               PR_IntervalToSeconds(now - mFirstPktSent) >= MAXTIME)) {
            LOG(("Test 5 finished: current time %lu, first packet sent at %lu, "
                 "duration %lu, sent %llu bytes, max bytes to send %llu",
                 now, mFirstPktSent,
                 PR_IntervalToSeconds(now - mFirstPktSent), mSentBytes,
                 MAXBYTES));
            mLastPktId = mNextPktId;
            FormatFinishPkt();
            mPhase = FINISH_PACKET;
          }
          int count = PR_SendTo(aFd, mSendBuf, PAYLOADSIZE, 0, &mNetAddr,
                                PR_INTERVAL_NO_WAIT);
          if (count < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR) {
              return 0;
            }
            return LogErrorWithCode(code, "UDP");
          }
          mSentBytes += count;

          if (mPhase != FINISH_PACKET) {
            // Calculate time to do something.
            mNextToSendInns += mPktInterval;
            mNextTimeToDoSomething = mFirstPktSent +
              PR_MicrosecondsToInterval(floor(mNextToSendInns / 1000.0));

            // Log
            sprintf(mLogstr, "%lu SEND %lu %lu\n",
                    (unsigned long)PR_IntervalToMilliseconds(now),
                    (unsigned long)mNextPktId,
                    (unsigned long)PR_IntervalToMilliseconds(mNextTimeToDoSomething));
            mLogFile.WriteNonBlocking(mLogstr, strlen(mLogstr));

          } else {
            // Calculate time to do something.
            mNextTimeToDoSomething = now +
              PR_MillisecondsToInterval(RETRANSMISSION_TIMEOUT);

            // Log
            sprintf(mLogstr, "%lu FIN %lu %lu\n",
                    (unsigned long)PR_IntervalToMilliseconds(now),
                    (unsigned long)mNextPktId,
                    (unsigned long)PR_IntervalToMilliseconds(mNextTimeToDoSomething));
          }
          mLogFile.WriteBlocking(mLogstr, strlen(mLogstr));

          mNextPktId++;
          if (mFirstPktSent == 0) {
            mFirstPktSent = now;
          }
        }
      }
      break;
    default:
      return -1;
  }

  return 0;
}

int
ClientSocket::SendFinishPacket(PRFileDesc *aFd)
{
  LOG(("NetworkTest UDP server side: Sending finish packet."));
  if (mNumberOfRetransFinish > MAX_RETRANSMISSIONS) {
    mError = true;
    mPhase = TEST_FINISHED;
    return 0;
  }

  PRIntervalTime now = PR_IntervalNow();
  FormatDataPkt(PR_IntervalToMilliseconds(now));
  FormatFinishPkt();
  int count = PR_SendTo(aFd, mSendBuf, PAYLOADSIZE, 0, &mNetAddr,
                        PR_INTERVAL_NO_WAIT);
  if (count < 1) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      return 0;
    }
    return LogErrorWithCode(code, "UDP");
  }
  mSentBytes += count;

  sprintf(mLogstr, "%lu FIN\n", (unsigned long)PR_IntervalToMilliseconds(now));
  mLogFile.WriteBlocking(mLogstr, strlen(mLogstr));

  LOG(("NetworkTest UDP sever side: Sending data for test %d"
       " - sent %lu bytes - received %lu bytes.",
       mTestType,  mSentBytes, mRecvBytes));
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
  LOG(("NetworkTest UDP server side: No data from the other side for too long -"
       " finish test."));
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
  PRIntervalTime received = PR_IntervalNow();

  // if we have not received packet for a long time we can assume a broken
  // connection.
  mLastReceivedTimeout = received + mNodataTimeout;

  // We can receive a data packet(test 6) or an ack(test 5) or a
  // packet describing the start of a new test.
  if (memcmp(aBuf + TYPE_START, TEST_prefix, 5) == 0) {
    // We have received a packet that has a format of the first.
    FirstPacket(aCount, aBuf, received);

  } else {
    if (mTestType == 0) {
      mError = true;
      mPhase = TEST_FINISHED;
      return 0;
    }

    switch (mTestType) {
      case 5:
        {
          mRecvBytes +=aCount;
          // Get packet Id.
          uint32_t pktId = ReadACKPktAndLog(aBuf,
                             PR_IntervalToMilliseconds(received));

          if (mPhase == FINISH_PACKET) {
            // Check if we got ACK for the finish packet.
            if (mLastPktId == pktId) {
              mPhase = WAIT_FINISH_TIMEOUT;
              mNextTimeToDoSomething = received +
                PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
            }
          }
        }
        break;
      case 6:
        mRecvBytes +=aCount;

        if (mPhase == RUN_TEST) {
          if (memcmp(aBuf + FINISH_START, FINISH, FINISH_LEN) == 0) {
            mPhase = WAIT_FINISH_TIMEOUT;
            mNextTimeToDoSomething = received +
                                     PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
            if (!mPktPerSecObserved &&
                PR_IntervalToSeconds(PR_IntervalNow() - mFirstPktReceived)) {
              mPktPerSecObserved = (double)mRecvBytes / PAYLOADSIZEF /
                (double)PR_IntervalToMilliseconds(PR_IntervalNow() - mFirstPktReceived)
                * 1000.0;
            }
            LOG(("NetworkTest UDP client: Closing, observed rate: %llu",
                  mPktPerSecObserved));
            LOG(("Test 6 finished: current time %lu, first packet sent %lu, "
                 "duration %lu, received %llu.",
                 PR_IntervalNow(),
                 mFirstPktReceived,
                 PR_IntervalToMilliseconds(PR_IntervalNow() - mFirstPktReceived),
                 mRecvBytes));
          }
        }

        // Send ack.
        mAcksToSend.push_back(Ack(aBuf, received, 0,
                                  mPktPerSecObserved));
      break;
    default:
      return -1;
    }
  }
  return 0;
}


int
ClientSocket::FirstPacket(int32_t aCount, char *aBuf, PRIntervalTime received)
{
  if (memcmp(mPktIdFirstPkt, aBuf + PKT_ID_START, PKT_ID_LEN) == 0) {
    LOG(("NetworkTest UDP server side: Received a dup of the first "
         "packet. %lu", mTestType));

    if (mTestType == 1) {
      mAcksToSend.push_back(Ack(aBuf, received, aCount, 0));
    } else if (mTestType == 5) {
      mNextTimeToDoSomething = received;
      sprintf(mLogstr, "%lu START TEST 5 DUP\n",
              (unsigned long)PR_IntervalToMilliseconds(received));
      mLogFile.WriteBlocking(mLogstr, strlen(mLogstr));
    } else if (mTestType == 6) {
      mAcksToSend.push_back(Ack(aBuf, received, 0, 0));
    }
    return 0;
  }

  //if it is not a duplicate start a new test.

  // If the last test is in WAIT_FINISH_TIMEOUT or not finished properly close
  // the report.
  if (mTestType != 0) {
    mLogFile.Done();
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
  memcpy(mPktIdFirstPkt, aBuf + PKT_ID_START, PKT_ID_LEN);
  mNextPktId = ntohl(*((uint32_t*)mPktIdFirstPkt)) + 1;
  mPktPerSecObserved = 0;
  mLastPktId = 0;
  mPhase = START_TEST;

  if (memcmp(aBuf + TYPE_START, UDP_reachability, TYPE_LEN) == 0) {

    mTestType = 1;
    mPhase = WAIT_FINISH_TIMEOUT;
    // Send a reply.
    mAcksToSend.push_back(Ack(aBuf, received, aCount, 0));
    mNextTimeToDoSomething = received +
                             PR_MillisecondsToInterval(SHUTDOWNTIMEOUT);
    LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));

  } else if (memcmp(aBuf + TYPE_START, UDP_performanceFromServerToClient,
                    TYPE_LEN) == 0) {

    mNextTimeToDoSomething = received;
    mTestType = 5;
    LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));
    mRecvBytes +=aCount;

    // Get desired rate.
    uint64_t npktpersec;
    memcpy(&npktpersec, aBuf + RATE_TO_SEND_START, RATE_TO_SEND_LEN);
    mPktPerSec = ntohll(npktpersec);
    if (mPktPerSec == 0) {
      mError = true;
      mPhase = TEST_FINISHED;
      return 0;
    }

    mPktInterval = 1000000000.0 / mPktPerSec; // the interval in ns.

    // Get file name.
    memcpy(mLogFileName, aBuf + FILE_NAME_START, FILE_NAME_LEN);
    LOG(("File name: %s", mLogFileName));
    mPhase = RUN_TEST;
    LOG(("NetworkTest UDP server side: Test %d: rate %d interval %lf.",
         mTestType, mPktPerSec, mPktInterval));
    if (mLogFile.Init(mLogFileName) < 0) {
      mError = true;
      mPhase = TEST_FINISHED;
      return 0;
    }
    LogLogFormat();

    sprintf(mLogstr, "%lu START TEST 5: rate %lu\n",
            (unsigned long)PR_IntervalToMilliseconds(received),
            (unsigned long)ntohl(npktpersec));
    mLogFile.WriteBlocking(mLogstr, strlen(mLogstr));

  } else if (memcmp(aBuf + TYPE_START, UDP_performanceFromClientToServer,
                    TYPE_LEN) == 0) {

    mAcksToSend.push_back(Ack(aBuf, received, 0, 0));
    mFirstPktReceived = received;
    mTestType = 6;
    LOG(("NetworkTest UDP server side: Starting test %d.", mTestType));

    mPhase = RUN_TEST;
  } else {
    LOG(("NetworkTest UDP server side: Test not implemented"));
    return -1;
  }
  return 0;
}

void
ClientSocket::FormatDataPkt(uint32_t aTS)
{
  // We do not do htonl for pkt id and timestamp because these values will be
  // only read by this host. They are stored in a packet, sent to the receiver,
  // the receiver copies them into an ACK pkt and sends them back to the sender
  // that copies them back into uint32_t variables.

  // Add pkt ID.
  memcpy(mSendBuf + PKT_ID_START, &mNextPktId, PKT_ID_LEN);

  // Add timestamp.
  memcpy(mSendBuf + TIMESTAMP_START, &aTS, TIMESTAMP_LEN);
}

void
ClientSocket::FormatFinishPkt()
{
  memcpy(mSendBuf + FINISH_START, FINISH, FINISH_LEN);
}

uint32_t
ClientSocket::ReadACKPktAndLog(char *aBuf, uint32_t aTS)
{
  // We do not do htonl for pkt id and timestamp because these values will be
  // only read by this host. They are stored in a packet, sent to the receiver,
  // the receiver copies them into an ACK pkt and sends them back to the sender
  // that copies them back into uint32_t variables.

  // Get packet Id.
  uint32_t pktId;
  memcpy(&pktId, aBuf + PKT_ID_START, PKT_ID_LEN);

  // Get timestamp.
  uint32_t ts;
  memcpy(&ts, aBuf + TIMESTAMP_START, TIMESTAMP_LEN);

  // Get the time the pkt was received at the receiver and the time the ACK
  // was sent.
  // The delay at receiver can be calculated from these values.
  uint32_t usecReceived;
  memcpy(&usecReceived, aBuf + TIMESTAMP_RECEIVED_START,
         TIMESTAMP_RECEIVED_LEN);
  uint32_t usecACKSent;
  memcpy(&usecACKSent, aBuf + TIMESTAMP_ACK_SENT_START, TIMESTAMP_ACK_SENT_LEN);

  sprintf(mLogstr, "%lu ACK %lu %lu %lu %lu\n",
          (unsigned long)aTS,
          (unsigned long)pktId,
          (unsigned long)ts,
          (unsigned long)ntohl(usecReceived),
          (unsigned long)ntohl(usecACKSent));
  mLogFile.WriteNonBlocking(mLogstr, strlen(mLogstr));
  return pktId;
}

void
ClientSocket::LogLogFormat()
{
  char line1[] = "Data pkt has been sent: [timestamp pkt sent] SEND [pkt id] [pkt are sent in \n"
                 "                        equal intervals log time when it should have been\n"
                 "                        sent(this is for the analysis whether the gap between\n"
                 "                        the time it should have been sent and the time it was\n"
                 "                        sent is too large)]\n";
  mLogFile.WriteBlocking(line1, strlen(line1));

  char line2[] = "The last packet has the same format as data packet\n";
  mLogFile.WriteBlocking(line2, strlen(line2));

  char line3[] = "An ACK has been received: [timestamp ack was received] ACK [pkt id]\n"
                 "                          [timestamp data pkt was sent by the sender (this\n"
                 "                          host)] [time when data packet was received by the\n"
                 "                          receiver] [time when ack was sent by the receiver]";
  mLogFile.WriteBlocking(line3, strlen(line3));
}
