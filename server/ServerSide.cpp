/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TCPserver.h"
#include "UDPserver.h"
#include "prlog.h"

PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)

uint64_t maxBytes = (1<<21);
uint32_t maxTime = 4; //TODO:chnge tthis to the 12s

int
main(int32_t argc, char *argv[])
{
  gServerTestLog = PR_NewLogModule("NetworkTestServer2");

  // todo this list ought to live in one place
  uint16_t ports[] = { 61590, 2708, 891, 443, 80 };
  const int numPorts = sizeof(ports) / sizeof(uint16_t);

  int rv;
  UDPserver udp;
  rv = udp.Start(ports, numPorts);
  if (rv) {
    return rv;
  }
  TCPserver tcp;
  rv = tcp.Start(ports, numPorts);
  if (rv) {
    return rv;
  }
}
