/*
 * Eagle Zhang Project
 * ezlist.h
 * Create on 2017-01-08
 * author : Eagle Zhang
 * Copyright (c) 2016 Eagle Zhang
 */

#ifndef __EZLIST_H__
#define __EZLIST_H__

#ifdef __cplusplus
extern "C"{
#endif

typedef struct EZList_Head_s
{
	struct EZList_Head_s *pPrev;
	struct EZList_Head_s *pNext;
}EZList_Head_t, EZList_Linker_t;

#define EZList_Head_Def(Head) EZList_Head_t Head = {&Head, &Head}

#define EZList_tHead_Init(tHead) \
	{(tHead).pPrev = &(tHead);(tHead).pNext = &(tHead);}

#define EZList_pHead_Init(pHead) \
	{(pHead)->pPrev = (pHead);(pHead)->pNext = (pHead);}

#define EZList_Empty(head) ((head)->pNext == (head))

#define EZList_Entry(pLinker, type, member) \
	((type*)((char*)(pLinker)-(unsigned long)(&((type*)0)->member)))

#define _EZList_Add(pNew, prev, next) \
	{\
		(next)->pPrev = (pNew);\
		(pNew)->pNext = (next);\
		(pNew)->pPrev = (prev);\
		(prev)->pNext = (pNew);\
	}

#define EZList_Add(pNew, pHead) \
	{\
		EZList_Head_t *pNewTmp = (pNew);\
		EZList_Head_t *pPrevTmp = (pHead);\
		EZList_Head_t *pNextTmp = (pHead)->pNext;\
		_EZList_Add(pNewTmp, pPrevTmp, pNextTmp)\
	}

#define EZList_Add_Tail(pNew, pHead) \
	{\
		EZList_Head_t *pNewTmp = (pNew);\
		EZList_Head_t *pPrevTmp = (pHead)->pPrev;\
		EZList_Head_t *pNextTmp = (pHead);\
		_EZList_Add(pNewTmp, pPrevTmp, pNextTmp)\
	}

#define _EZList_Del(prev, next) \
	{\
		(next)->pPrev = (prev);\
		(prev)->pNext = (next);\
	}

/**
 * delete a node from list
 * @param pNode The node to delete, point to EZList_Linker_t 
 */
#define EZList_Del(pNode) \
	{\
		EZList_Head_t *pPrevTmp = (pNode)->pPrev;\
		EZList_Head_t *pNextTmp = (pNode)->pNext;\
		_EZList_Del(pPrevTmp, pNextTmp)\
		EZList_pHead_Init((pNode))\
	}

/**
 * splice 2 list. The source header would be set to empty after splice
 * @param pDstHeader
 * @param pDstHeader
 */
#define EZList_Splice(pDstHeader, pSrcHeader) \
    {\
        if(!EZList_Empty(pSrcHeader)) {\
        EZList_Linker_t *pDstfirstNode = (pDstHeader)->pNext;\
        EZList_Linker_t *pDstLastNode = (pDstHeader)->pPrev;\
        EZList_Linker_t *pSrcfirstNode = (pSrcHeader)->pNext;\
        EZList_Linker_t *pSrcLastNode = (pSrcHeader)->pPrev;\
        (pDstHeader)->pPrev = pSrcLastNode;\
        pDstLastNode->pNext = pSrcfirstNode;\
        pSrcfirstNode->pPrev = pDstLastNode;\
        pSrcLastNode->pNext = pDstHeader;\
        EZList_pHead_Init(pSrcHeader);}\
    }

#define EZList_ForEach(pLinker, pHead) \
	for((pLinker) = (pHead)->pNext;(pLinker) != (pHead);(pLinker) = (pLinker)->pNext)

#define EZList_ForEach_Prev(pLinker, pHead) \
	for((pLinker) = (pHead)->pPrev;(pLinker) != (pHead);(pLinker) = (pLinker)->pPrev)

#define EZList_ForEach_Safe(pLinker, pTmp, pHead) \
	for((pLinker) = (pHead)->pNext,(pTmp) = (pLinker)->pNext;(pLinker) != (pHead);(pLinker) = (pTmp), (pTmp) = (pLinker)->pNext)

#define EZList_ForEach_Prev_Safe(pLinker, pTmp, pHead) \
	for((pLinker) = (pHead)->pPrev,(pTmp) = (pLinker)->pPrev;(pLinker) != (pHead);(pLinker) = (pTmp), (pTmp) = (pLinker)->pPrev)


#ifdef __cplusplus
}
#endif

#endif

