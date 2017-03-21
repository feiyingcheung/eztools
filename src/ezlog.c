/*
 * Eagle Zhang Project
 * ezlog.c
 * Create on 2017-01-08
 * author : Eagle Zhang
 * Copyright (c) 2016 Eagle Zhang
 */
#if 0
#if defined(_WIN32) && defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
//#define _CRTDBG_MAP_ALLOC_NEW
#include <crtdbg.h>
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apr.h"
#include "apr_file_info.h"
#include "apr_poll.h"
#include "apr_thread_cond.h"
#include "apr_thread_mutex.h"
#include "apr_thread_proc.h"
#include "apr_time.h"

#include "ezlist.h"
#include "ezlog.h"

#define EZLOG_MAX_LOG_LEN 512
#define EZLOG_MAX_RECYCLE_ITEM 100
#define EZLOG_MAX_LOG_ITEM 150
#define EZLOG_TIMESTAMP_LEN 15
#define EZLOG_MODTAG_LEN (EZLOG_MODNAME_LEN + 2)
#define EZLOG_MODFMT_LEN 10
#define EZLOG_HEADER_LEN (1 + EZLOG_MODTAG_LEN + EZLOG_TIMESTAMP_LEN)
#define EZLOG_FILE_SIZE (30 * 1024 * 1024)
//#define EZLOG_FILE_SIZE (1*1024*1024) //for testing

#ifdef _WIN32
#define snprintf sprintf_s
//#define vsnprintf vsprintf_s
#endif
typedef struct _EZLogItem_s {
    EZList_Linker_t tLinker;
    char aStrBuf[EZLOG_MAX_LOG_LEN + 2];
    int iLen;
} EZLogItem_t;

struct EZLog_t {
    EZList_Head_t tItemHead;
    EZList_Head_t tRecyleItemHead;
    int iItemCnt;
    apr_pool_t *pPool;
    apr_thread_mutex_t *pMutex;
    apr_thread_cond_t *pRdCond;
    apr_thread_cond_t *pWrCond;
    apr_thread_t *pThread;
    int iCondWaitRdCnt;
    int iCondWaitWrCnt;
    int iLogLevel;
    int iConsole;
    int iDestroy;
};

static int g_iEZLog_Init = 0;
static char g_aModTagFmt[EZLOG_MODFMT_LEN + 1];
static char g_aLogType[] = "EWID";
static apr_pool_t *g_pEZLog_Pool = NULL;
static EZLog_t *g_pEZLog_Def = NULL;

static int iEZLog_GetFileNameByTime(char *pPath, char *pFileNameBuf) {
    apr_time_exp_t tTime;
    apr_time_exp_lt(&tTime, apr_time_now());
    snprintf(pFileNameBuf, 256, "%s/%04d%02d%02d_%02d%02d%02d.log", pPath,
             tTime.tm_year + 1900, tTime.tm_mon + 1, tTime.tm_mday,
             tTime.tm_hour, tTime.tm_min, tTime.tm_sec);
    return 0;
}

static int iEZLog_CreateLogFile(apr_file_t **pLogFile, apr_pool_t **pFilePool,
                                apr_pool_t *pPool) {
    apr_status_t aprRet = APR_SUCCESS;
    char aFileName[256] = {0};
    aprRet = apr_pool_create(pFilePool, pPool);
    if (aprRet != APR_SUCCESS) {
        return -1;
    }

    iEZLog_GetFileNameByTime("log", aFileName);
    aprRet =
        apr_file_open(pLogFile, aFileName, APR_CREATE | APR_WRITE | APR_APPEND,
                      APR_OS_DEFAULT, *pFilePool);
    if (aprRet != APR_SUCCESS) {
        apr_pool_destroy(*pFilePool);
        pFilePool = NULL;
        return -1;
    }

    return 0;
}

static int iEZLog_CloseLogFile(apr_file_t **pLogFile, apr_pool_t **pFilePool) {
    if (*pLogFile != NULL) {
        apr_file_close(*pLogFile);
        *pLogFile = NULL;
    }
    if (*pFilePool != NULL) {
        apr_pool_destroy(*pFilePool);
        *pFilePool = NULL;
    }
    return 0;
}

static int iEZLog_RecreateLogFile(apr_file_t **pLogFile, apr_pool_t **pFilePool,
                                  apr_pool_t *pPool) {
    int iRet = iEZLog_CloseLogFile(pLogFile, pFilePool);
    iRet = iEZLog_CreateLogFile(pLogFile, pFilePool, pPool);
    return 0;
}

static int iEZLog_CreatePath() {
    apr_status_t aprRet = APR_SUCCESS;
    aprRet = apr_dir_make("log", APR_OS_DEFAULT, g_pEZLog_Pool);
    if ((aprRet) && APR_STATUS_IS_EEXIST(aprRet)) {
        return 0;
    }
    return aprRet;
}

static void *APR_THREAD_FUNC iEZLog_Thread(apr_thread_t *pThread, void *pData) {
    // int iRet = 0;
    apr_pool_t *pFilePool = NULL;
    apr_file_t *pFile = NULL;
    apr_status_t aprRet = APR_SUCCESS;
    EZLog_t *pEZLog = (EZLog_t *)pData;
    EZList_Head_t tTmpList;
    int iTmpItemCnt = 0;
    EZList_Head_t *pLinker = NULL;
    EZList_Head_t *pLinkerTmp = NULL;
    EZLogItem_t *pItem = NULL;
    int iFileSize = 0;
    EZList_tHead_Init(tTmpList);
    iEZLog_CreatePath();
    iEZLog_CreateLogFile(&pFile, &pFilePool, pEZLog->pPool);
    while (1) {
        apr_thread_mutex_lock(pEZLog->pMutex);
        if (EZList_Empty(&pEZLog->tItemHead)) {
            pEZLog->iCondWaitRdCnt++;
            aprRet = apr_thread_cond_timedwait(pEZLog->pRdCond, pEZLog->pMutex,
                                               1000 * 1000);
            pEZLog->iCondWaitRdCnt--;
            if (aprRet != APR_SUCCESS) {
                apr_thread_mutex_unlock(pEZLog->pMutex);
                if (aprRet != APR_TIMEUP) {
                    apr_sleep(1000 * 1000);
                }
                if (pEZLog->iDestroy) {
                    break;
                }
                continue;
            }
        }
        EZList_Splice(&tTmpList, &pEZLog->tItemHead);
        apr_thread_mutex_unlock(pEZLog->pMutex);

        // write log
        iTmpItemCnt = 0;
        EZList_ForEach_Safe(pLinker, pLinkerTmp, &tTmpList) {
            pItem = EZList_Entry(pLinker, EZLogItem_t, tLinker);
            if (pFile != NULL) {
                apr_file_write_full(pFile, pItem->aStrBuf, pItem->iLen, NULL);
                iFileSize += pItem->iLen;
                if (iFileSize > EZLOG_FILE_SIZE) {
                    iEZLog_RecreateLogFile(&pFile, &pFilePool, pEZLog->pPool);
                    iFileSize = 0;
                }
            }
            if (pEZLog->iConsole != 0) {
                printf(pItem->aStrBuf);
            }
            iTmpItemCnt++;
        }

        // recycle log item
        while (pEZLog->iItemCnt >= EZLOG_MAX_RECYCLE_ITEM) {
            if (EZList_Empty(&tTmpList)) {
                break;
            } else {
                pLinker = tTmpList.pNext;
                pItem = EZList_Entry(pLinker, EZLogItem_t, tLinker);
                EZList_Del(pLinker);
                free(pItem);
                pEZLog->iItemCnt--;
            }
        }

        apr_thread_mutex_lock(pEZLog->pMutex);
        EZList_Splice(&pEZLog->tRecyleItemHead, &tTmpList);
        if (pEZLog->iCondWaitWrCnt) {
            apr_thread_cond_signal(pEZLog->pWrCond);
        }
        apr_thread_mutex_unlock(pEZLog->pMutex);
    }
    iEZLog_CloseLogFile(&pFile, &pFilePool);
    apr_thread_exit(pThread, APR_SUCCESS);
    return NULL;
}

static EZLogItem_t *iEZLog_GetItem(EZLog_t *pEZLog) {
    apr_status_t aprRet = APR_SUCCESS;
    EZLogItem_t *pItem = NULL;
    if (EZList_Empty(&pEZLog->tRecyleItemHead)) {
        if (pEZLog->iItemCnt < EZLOG_MAX_LOG_ITEM) {
            pItem = (EZLogItem_t *)malloc(sizeof(EZLogItem_t));
            if (pItem) {
                pEZLog->iItemCnt++;
            }
        } else {
            pEZLog->iCondWaitWrCnt++;
            aprRet = apr_thread_cond_wait(pEZLog->pWrCond, pEZLog->pMutex);
            pEZLog->iCondWaitWrCnt--;
            if (aprRet == APR_SUCCESS) {
                if (!EZList_Empty(&pEZLog->tRecyleItemHead)) {
                    EZList_Head_t *pLinker = pEZLog->tRecyleItemHead.pNext;
                    pItem = EZList_Entry(pLinker, EZLogItem_t, tLinker);
                    EZList_Del(pLinker);
                }
            } else {
                char aErrMsg[64];
                apr_strerror(aprRet, aErrMsg, 64);
                printf(aErrMsg);
            }
        }
    } else {
        EZList_Head_t *pLinker = pEZLog->tRecyleItemHead.pNext;
        pItem = EZList_Entry(pLinker, EZLogItem_t, tLinker);
        EZList_Del(pLinker);
    }
    return pItem;
}

static void iEZLog_Push(EZLog_t *pEZLog, EZLogItem_t *pItem) {
    if (pEZLog->iItemCnt > EZLOG_MAX_LOG_ITEM) {
        ;
    }
    EZList_Add_Tail(&pItem->tLinker, &pEZLog->tItemHead);
    if (pEZLog->iCondWaitRdCnt) {
        apr_thread_cond_signal(pEZLog->pRdCond);
    }
}

static void iEZLog_ConstructHeader(char *pStrBuf, EZLog_t *pEZLog,
                                   const char *pModName, int iLogLevel) {
    char *pBuf;
    apr_time_exp_t tTime;
    apr_time_exp_lt(&tTime, apr_time_now());
    pBuf = pStrBuf;
    *pBuf++ = g_aLogType[iLogLevel];
    snprintf(pBuf, EZLOG_MODTAG_LEN + 1, g_aModTagFmt, pModName);
    pBuf += EZLOG_MODTAG_LEN;
    snprintf(pBuf, EZLOG_TIMESTAMP_LEN + 1, "[%02d:%02d:%02d.%03d] ",
             tTime.tm_hour, tTime.tm_min, tTime.tm_sec, tTime.tm_usec / 1000);
    return;
}

int EZLog_Init(EZLog_t **ppEZLog, EZLog_InitParam_t *pParam) {
    EZLog_t *pEZLog = NULL;
    apr_status_t aprRet = APR_SUCCESS;
    if (g_iEZLog_Init == 0) {
        // init apr;
        aprRet = apr_initialize();
        aprRet = apr_pool_create(&g_pEZLog_Pool, NULL);
        snprintf(g_aModTagFmt, EZLOG_MODFMT_LEN, "[%%- %ds]",
                 EZLOG_MODNAME_LEN);
        g_iEZLog_Init = 1;
    }

    pEZLog = (EZLog_t *)malloc(sizeof(EZLog_t));
    if (pEZLog == NULL) {
        printf("E[EZLog_Init]malloc failed!\n");
        return -1;
    }
    memset(pEZLog, 0, sizeof(EZLog_t));

    EZList_tHead_Init(pEZLog->tItemHead);
    EZList_tHead_Init(pEZLog->tRecyleItemHead);
    aprRet = apr_pool_create(&pEZLog->pPool, g_pEZLog_Pool);
    aprRet = apr_thread_mutex_create(&pEZLog->pMutex, APR_THREAD_MUTEX_DEFAULT,
                                     pEZLog->pPool);
    aprRet = apr_thread_cond_create(&pEZLog->pRdCond, pEZLog->pPool);
    aprRet = apr_thread_cond_create(&pEZLog->pWrCond, pEZLog->pPool);
    aprRet = apr_thread_create(&pEZLog->pThread, NULL, iEZLog_Thread, pEZLog,
                               pEZLog->pPool);
    if (ppEZLog != NULL) {
        *ppEZLog = pEZLog;
    }
    return 0;
}

int EZLog_Uninit(EZLog_t *pEZLog) {
    apr_status_t aprRet = APR_SUCCESS;
    pEZLog->iDestroy = 1;
    apr_thread_join(&aprRet, pEZLog->pThread);
    apr_pool_destroy(pEZLog->pPool);
    return 0;
}

int vEZLog(EZLog_t *pEZLog, const char aModName[EZLOG_MODNAME_LEN],
           int iLogLevel, const char *fmt, va_list args) {
    int iRet = 0;
    EZLogItem_t *pItem = NULL;
    char aStrBuf[EZLOG_MAX_LOG_LEN + 2];
    iEZLog_ConstructHeader(aStrBuf, pEZLog, aModName, iLogLevel);
    iRet = vsnprintf(aStrBuf + EZLOG_HEADER_LEN,
                     EZLOG_MAX_LOG_LEN - EZLOG_HEADER_LEN, fmt, args);
    iRet += EZLOG_HEADER_LEN;
    aStrBuf[EZLOG_MAX_LOG_LEN] = '\0';
    apr_thread_mutex_lock(pEZLog->pMutex);
    do {
        pItem = iEZLog_GetItem(pEZLog);
    } while (pItem == NULL);
    if (pItem != NULL) {
        memcpy(pItem->aStrBuf, aStrBuf, iRet + 1);
        pItem->iLen = iRet;
        iEZLog_Push(pEZLog, pItem);
    }
    apr_thread_mutex_unlock(pEZLog->pMutex);
    return iRet;
}

int EZLog(EZLog_t *pEZLog, const char aModName[EZLOG_MODNAME_LEN],
          int iLogLevel, const char *fmt, ...) {
    int iRet = 0;
    va_list args;
    if (iLogLevel > pEZLog->iLogLevel) {
        return 0;
    }
    if (iLogLevel > EZLOG_DEBUG) {
        iLogLevel = EZLOG_DEBUG;
    }
    va_start(args, fmt);
    iRet = vEZLog(pEZLog, aModName, iLogLevel, fmt, args);
    va_end(args);
    return iRet;
}

int EZLog_SetLogLevel(EZLog_t *pEZLog, int iLogLevel) {
    if (iLogLevel > EZLOG_DEBUG) {
        pEZLog->iLogLevel = EZLOG_DEBUG;
    } else {
        pEZLog->iLogLevel = iLogLevel;
    }
    return 0;
}

int EZLog_SetConsoleLog(EZLog_t *pEZLog, int iConsole) {
    pEZLog->iConsole = iConsole;
    return 0;
}
