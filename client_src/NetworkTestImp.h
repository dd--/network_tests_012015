/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_NetworkTestImp
#define mozilla_net_NetworkTestImp

#include "NetworkTest.h"
#include "prnetdb.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "nsIThread.h"

namespace NetworkPath {

class NetworkTestImp MOZ_FINAL : public NetworkTest
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NetworkTestImp();
  NS_IMETHOD RunTest(NetworkTestListener *aListener);

  void AllTests();

private:
  static const int kNumberOfPorts = 5;
  static const uint16_t mPorts[kNumberOfPorts];

  ~NetworkTestImp();
  int GetHostAddr(nsAutoCString &aAddr);
  nsresult GetNextAddr(PRNetAddr *aAddr);
  void AddPort(PRNetAddr *aAddr, uint16_t aPort);
  nsresult Test1(PRNetAddr *aNetAddr);
  nsresult Test2(PRNetAddr *aNetAddr);
  nsresult Test3a(PRNetAddr *aNetAddr,
                  uint16_t aRemotePort);
  nsresult Test3b(PRNetAddr *aNetAddr,
                  uint16_t aRemotePort);

  void TestsFinished();
  PRAddrInfo *mAddrInfo;
  void *mIter;
  bool mTCPReachabilityResults[kNumberOfPorts];
  bool mUDPReachabilityResults[kNumberOfPorts];
  nsCOMPtr<NetworkTestListener> mCallback;
  nsCOMPtr<nsIThread> mThread;
};

} // namespace NetworkPath
#endif // mozilla_net_NetworkTestImp
