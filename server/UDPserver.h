/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TEST_UDP_SERVER_SIDE_H__
#define TEST_UDP_SERVER_SIDE_H__

#include "prio.h"
#include "prerror.h"
#include "prthread.h"

class UDPserver
{
public:
  UDPserver();
  ~UDPserver();
  int Start(uint16_t *aPort, int aNumberOfPorts);

private:
  int Init(uint16_t aPort, int aInx);

  PRThread **mThreads;
  int mNumberOfPorts;
};

#endif
