/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NetworkTest.h"
#include "prnetdb.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "nsIThread.h"

class NetworkTestImp MOZ_FINAL : public NetworkTest
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NetworkTestImp();
  NS_IMETHOD RunTest(NetworkTestListener *aListener);

  void AllTests();

private:
  ~NetworkTestImp();
  int GetHostAddr(nsAutoCString &aAddr);
  nsresult GetNextAddr(PRNetAddr *aAddr);
  void AddPort(PRNetAddr *aAddr, uint16_t aPort);
  nsresult Test1(PRNetAddr *aNetAddr);
  nsresult Test2(PRNetAddr *aNetAddr);
  nsresult Test3a(PRNetAddr *aNetAddr, uint16_t aLocalPort,
                  uint16_t aRemotePort);
  nsresult Test3b(PRNetAddr *aNetAddr, uint16_t aLocalPort,
                  uint16_t aRemotePort);

  void TestsFinished();
  PRAddrInfo *mAddrInfo;
  void *mIter;
  bool *mTCPReachabilityResults;
  bool *mUDPReachabilityResults;
  nsCOMPtr<NetworkTestListener> mCallback;
  nsCOMPtr<nsIThread> mThread;
};
