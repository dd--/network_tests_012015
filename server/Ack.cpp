/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Ack.h"
#include "HelpFunctions.h"
#include <cstring>

Ack::Ack(char *aBuf, PRIntervalTime aRecv, int aLargeAck, uint32_t aRate)
{
  if (aLargeAck) {
    if (aLargeAck < 512) {
      aLargeAck = 512;
    }
    mBufLen = aLargeAck;
  } else if (aRate) {
    mBufLen = pktIdLen + tsLen + delayLen + rateLen;
  } else {
    mBufLen = pktIdLen + tsLen + delayLen;
  }
  mBuf = new char[mBufLen];
  memcpy(mBuf, aBuf, mBufLen);
  uint32_t rate = htonl(aRate);
  memcpy(mBuf + rateStart, &rate, rateLen);
  mRecvTime = aRecv;
}

Ack::~Ack()
{
  delete [] mBuf;
}

Ack::Ack(const Ack &other)
{
  mBufLen = other.mBufLen;
  mBuf = new char[mBufLen];
  memcpy(mBuf, other.mBuf, mBufLen);
  mRecvTime = other.mRecvTime;
}

Ack&
Ack::operator= (const Ack &other)
{
  if (this != &other) {
    mBufLen = other.mBufLen;
    delete []mBuf;
    mBuf = new char[mBufLen];
    memcpy(mBuf, other.mBuf, mBufLen);
    mRecvTime = other.mRecvTime;
  }
  return *this;
}

int
Ack::SendPkt(PRFileDesc *aFd, PRNetAddr *aNetAddr)
{
  uint32_t sec = htonl(PR_IntervalToMilliseconds(PR_IntervalNow() - mRecvTime));
  memcpy(mBuf + delayStart, &sec, delayLen);
  int write = PR_SendTo(aFd, mBuf, mBufLen, 0, aNetAddr,
                        PR_INTERVAL_NO_WAIT);
  if (write < 1) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      return code;
    }
    return LogErrorWithCode(code, "UDP");
  }
  return 0;
}
