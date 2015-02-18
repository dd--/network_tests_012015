/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ACK_STRUCTURE_H__
#define ACK_STRUCTURE_H__

#include "prio.h"


extern int pktIdStart;
extern int tsStart; //timestamp
extern int typeStart; //type.
extern int delayStart; //delay.
extern int rateStart; //observed rate 
extern int pktIdLen;
extern int tsLen; //timestamp
extern int typeLen; //type
extern int delayLen; //delay
extern int rateLen;

class Ack
{
public:
  Ack(char *aBuf, PRIntervalTime aRecv);
  ~Ack();
  Ack(const Ack &other);
  Ack& operator= (const Ack &other);
  int SendPkt(PRFileDesc *aFd, PRNetAddr *aNetAddr);

private:
  // Ack pkt structure:  4+4+4byte packet id, copied timestamp, time between
  // receiving a packet and sending the ack in milliseconds.
  char *mBuf;
  PRIntervalTime mRecvTime;
  int mBufLen;
};

#endif
