/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TEST_UDP_CLIENT_SIDE_H__
#define TEST_UDP_CLIENT_SIDE_H__

#include "Ack.h"
#include "prnetdb.h"
#include <vector>

class UDP
{
public:
  UDP(PRNetAddr *aAddr, uint16_t aLocalPort);
  ~UDP();
  nsresult Start(int aTestType, uint32_t aRate, bool &aSucceeded);
  uint32_t GetRate() { return mRateObserved; }
private:
  nsresult Init();
  nsresult Run();
  nsresult StartTestSend();
  nsresult RunTestSend();
  nsresult SendFinishPacket();
  nsresult WaitForFinishTimeout();
  nsresult NoDataForTooLong();
  nsresult NewPkt(int32_t aCount, char *aBuf);
  nsresult SendAcks();

private:
  PRFileDesc *mFd;
  PRNetAddr mNetAddr;
  uint16_t mLocalPort;
  uint32_t mRate;
  uint32_t mRateObserved;
  int mTestType;
  PRIntervalTime mLastReceivedTimeout;
  PRIntervalTime mNextTimeToDoSomething;
  uint64_t mSentBytes;
  uint64_t mRecvBytes;
  std::vector<Ack> mAcksToSend;
  int mNumberOfRetrans;
  int mNumberOfRetransFinish;
  uint32_t mPktPerSec;
  double mPktInterval;
  double mNextToSendInns;
  char mPktIdFirstPkt[4];
  uint32_t mNextPktId;
  uint32_t mLastPktId;
  PRIntervalTime mFirstPktSent;
  PRIntervalTime mFirstPktReceived;
  PRIntervalTime mNodataTimeout;
  bool mError;

  enum PHASE {
    START_TEST,
    RUN_TEST,
    FINISH_PACKET,
    WAIT_FINISH_TIMEOUT,
    TEST_FINISHED
  };

  enum PHASE mPhase;
};

#endif
