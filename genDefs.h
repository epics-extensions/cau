#ifndef INC_genDefs_h
#define INC_genDefs_h
/*	$Id$
 *	Author:	Roger A. Cole
 *	Date:	03/29/90
 *
 *	Experimental Physics and Industrial Control System (EPICS)
 *
 *	Copyright 1991, the Regents of the University of California,
 *	and the University of Chicago Board of Governors.
 *
 *	This software was produced under  U.S. Government contracts:
 *	(W-7405-ENG-36) at the Los Alamos National Laboratory,
 *	and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *	Initial development by:
 *		The Controls and Automation Group (AT-8)
 *		Ground Test Accelerator
 *		Accelerator Technology Division
 *		Los Alamos National Laboratory
 *
 *	Co-developed with
 *		The Controls and Computing Group
 *		Accelerator Systems Division
 *		Advanced Photon Source
 *		Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 * .00	03/29/90	rac	initial version
 * .01	06-19-91	rac	installed in SCCS
 *
 * make options
 *	-DvxWorks	makes a version for VxWorks
 *	-DNDEBUG	don't compile assert() checking
 *      -DDEBUG         compile various debug code, including checks on
 *                      malloc'd memory
 */
/*+/mod***********************************************************************
* TITLE	genDefs.h - some generally useful definitions
*
* DESCRIPTION
*	This file contains some definitions which are generally useful for
*	EPICS programs.
*
* SEE ALSO
*	cmdSubr.c  genSubr.c  helpSubr.c  nextFieldSubr.c  tsSubr.c
*
*-***************************************************************************/
#ifdef vxWorks
/*----------------------------------------------------------------------------
*    includes for VxWorks compile
*---------------------------------------------------------------------------*/
#    include <vxWorks.h>
#    include <stdioLib.h>
#else
/*----------------------------------------------------------------------------
*    includes and definitions for Sun compile
*---------------------------------------------------------------------------*/
#   include <stdio.h>
#   ifndef OK
#       define OK 0
#   endif
#   ifndef ERROR
#       define ERROR -1
#   endif
#endif

/*----------------------------------------------------------------------------
*    some compatibility definitions for SunOS vs. VxWorks
*---------------------------------------------------------------------------*/
#ifdef vxWorks
#   define unlink(name) delete(name)
#endif

/*----------------------------------------------------------------------------
* common TYPEDEF's
*---------------------------------------------------------------------------*/
#define GEN_FNAME_DIM 80	/* dimension for file name strings */
#ifndef vxWorks
    typedef unsigned char	UCHAR;
    typedef unsigned short	USHORT;
    typedef unsigned long	ULONG;
#endif


/*+/macro---------------------------------------------------------------------
* NAME assert()
*
* DESCRIPTION
*	assert() evaluates an expression.  If the expression is non-zero
*	(i.e., "true"), then no action is taken.  If the expression is zero,
*	then the file name and line number are printed to stderr and an
*	abort() (under SunOS) or SIGUSR1 (under VxWorks) is done.
*
*	If a #define NDEBUG has been done, then assert() does nothing;
*	because of this, care must be taken that the expression has no
*	side-effects.
*
*	assertAlways() operates in a similar way, except it isn't
*	affected by defining NDEBUG.
*
* SYNOPSIS
*	void assert(expression)
*	void assertAlways(expression)
*
* EXAMPLES
*	assert(pBuf != NULL);
*	assert(strlen(name) < 20), printf("%s\n", name);
*
*---------------------------------------------------------------------------*/
int assertFail();
#ifndef NDEBUG
#   define assert(expr) ((void)((expr) || assertFail(__FILE__, __LINE__)))
#else
#   define assert(expr) ((void)0)
#endif
#define assertAlways(expr) ((void)((expr) || assertFail(__FILE__, __LINE__)))

/*+/macro---------------------------------------------------------------------
* NAME DoubleListXxx
*
* DESCRIPTION
*	The DoubleListXxx macros handle doubly linked lists, where the
*	lists and list pointers have the following form:
*
*	struct list {
*		.
*		.
*	    struct list *pPrev;		pointer to previous item
*	    struct list *pNext;		pointer to next item
*		.
*		.
*	}
*
*	struct list *pHead;		pointer to head item on list
*	struct list *pTail;		pointer to tail item on list
*
*	`list' is an arbitrary name used here for illustrative purposes; in
*	practice, a more meaningful name would be chosen.
*
* DoubleListAppend(pItem, pListHead, pListTail)
*       append an item to the end of a doubly linked list
*
* DoubleListRemove(pItem, pListHead, pListTail)
*       remove an item from a doubly linked list
*---------------------------------------------------------------------------*/
#define DoubleListAppend(pItem,pHead,pTail) \
{\
    pItem->pNext = NULL;\
    pItem->pPrev = pTail;\
    if (pTail != NULL)\
        pTail->pNext = pItem;         /* link previous tail to here */\
    pTail = pItem;\
    if (pHead == NULL)\
        pHead = pItem;                /* link to head if first item */\
}

#define DoubleListRemove(pItem,pHead,pTail) \
{\
    if (pItem->pPrev != NULL)\
        (pItem->pPrev)->pNext = pItem->pNext;   /* link prev to next */\
    else\
        pHead = pItem->pNext;                   /* link list head to next */\
    if (pItem->pNext != NULL)\
        (pItem->pNext)->pPrev = pItem->pPrev;   /* link next to prev */\
    else\
        pTail = pItem->pPrev;           /* link list tail to prev */\
    pItem->pNext = NULL;\
    pItem->pPrev = NULL;\
}


/*+/subhead helpXxx-----------------------------------------------------------
* NAME helpXxx - tools for implementing help capability
*---------------------------------------------------------------------------*/
typedef struct helpTopic {
    struct helpTopic *pNextTopic;/* ptr to next topic (NULL for follow-on) */
    char	*keyword;	/* text string for topic (NULL for follow-on) */
    struct helpTopic *pNextItem;/* next item for topic */
    struct helpTopic *pLastItem;/* last item for topic (NULL for follow-on) */
    char	*text;		/* help text string for item */
} HELP_TOPIC;

typedef struct {
    HELP_TOPIC	*pHead;		/* ptr to head of topic list */
    HELP_TOPIC	*pTail;		/* ptr to tail of topic list */
} HELP_LIST;

void helpIllegalCommand();
void helpInit();
void helpPrintTopics();
void helpTopicAdd();
HELP_TOPIC *helpTopicFind();
void helpTopicPrint();


/*+/subr----------------------------------------------------------------------
* NAME  GenMalloc/GenFree
*
* malloc and free macros for helping check and debug memory allocation.
*	If DEBUG is set, then allocated memory will have a `guard' field at
*	the beginning and end of the memory block. GenBufCheck and GenFree
*	check for violation of the guard fields.
*
*	If DEBUG is not set, then the macros are ordinary calls to malloc
*	and free; GenBufCheck will generate no code.
*
*	See genSubr.c for more details on the actual routines behind these
*	macros.
*
*	char *GenMalloc(nBytes)
*	void GenBufCheck(pBuf)
*	void GenFree(pBuf)
*---------------------------------------------------------------------------*/

#ifdef DEBUG
#   define GenMalloc(nBytes) genMalloc(nBytes)
#   define GenBufCheck(ptr) genBufCheck(ptr)
#   define GenFree(ptr) genFree(ptr)
#else
#   define GenMalloc(nBytes) malloc(nBytes)
#   define GenBufCheck(ptr)
#   define GenFree(ptr) free(ptr)
#endif

/*-----------------------------------------------------------------------------
* prototypes
*----------------------------------------------------------------------------*/
void genBufCheck();
void genFree();
char *genMalloc();
#ifndef vxWorks
void genShellCommand();
#endif
void genSigInit();

#endif
