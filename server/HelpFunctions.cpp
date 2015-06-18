/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HelpFunctions.h"
#include "prmem.h"
#include "prlog.h"
#include "config.h"
#include <cstring>

extern PRLogModuleInfo* gServerTestLog;
#define LOG(args) PR_LOG(gServerTestLog, PR_LOG_DEBUG, args)

int
LogErrorWithCode(PRErrorCode errCode, const char *aType)
{
  int errLen = PR_GetErrorTextLength();
  char *errStr = (char*)PR_MALLOC(errLen);
  if (errLen > 0) {
    PR_GetErrorText(errStr);
  }
  LOG(("NetworkTest %s server side:  error %x %s, %x", aType, errCode, errStr,
       PR_GetOSError()));
  delete [] errStr;
  return errCode;
}

int
LogError(const char *aType)
{
  PRErrorCode errCode = PR_GetError();
  return LogErrorWithCode(errCode, aType);
}

PRFileDesc*
OpenTmpFileForDataCollection(char *aFileName)
{
  char fileName[FILE_NAME_LEN + sizeof(TMP_DIRECTORY)];
  memcpy(fileName, TMP_DIRECTORY, sizeof(TMP_DIRECTORY));
  memcpy(fileName + sizeof(TMP_DIRECTORY) - 1, aFileName, FILE_NAME_LEN);

  PR_MkDir(TMP_DIRECTORY, 0777);
  return PR_Open(fileName, PR_CREATE_FILE | PR_WRONLY, 0666);
}
