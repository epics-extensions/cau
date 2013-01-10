/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution. 
\*************************************************************************/
/*	@(#)cau.c	1.13 8/11/93
 *	Author:	Roger A. Cole
 *	Date:	10-11-90
 *
 *	Experimental Physics and Industrial Control System (EPICS)
 *
 * make options
 *	-DvxWorks	makes a version for VxWorks
 *	-DNDEBUG	don't compile assert() checking
 */
/*+/mod***********************************************************************
* TITLE	cau - channel access utility
*
* DESCRIPTION
*	`cau' is a utility which provides some commonly used Channel Access
*	capabilities.  These include:
*	o  storing a new value for a channel
*	o  getting the present value for one or more channels
*	o  sending software signal generator output(s) to one or more
*	   channels
*	o  monitoring one or more channels
*	o  some simple tests for monitored channels
*
* WISH LIST
* o	handle waveforms--put and signal generation
* o	investigate usefulness of additional commands: snapshot, restore,
*	pause (waiting for operator), delay timeInterval, assert condition,
*	abort (as script), waitUntil condition
*
*-***************************************************************************/

#include <string.h>
#include <stdlib.h>
#include "genDefs.h"
#include "cmdDefs.h"
#include "cadef.h"
#include "db_access.h"
#include "nextFieldSubrDefs.h"
#include "cvtNumbersDefs.h"
#include "epicsTime.h"

#ifdef vxWorks
/*----------------------------------------------------------------------------
*    includes and defines for VxWorks compile
*---------------------------------------------------------------------------*/
#   include <vxWorks.h>
#   include <stdioLib.h>
#   include <ctype.h>
#   include <sigLib.h>
#   include <setjmp.h>
#   include <taskLib.h>
#   include <genTasks.h>
#   define MAXPRIO 160
#else
/*----------------------------------------------------------------------------
*    includes and defines for Sun compile
*---------------------------------------------------------------------------*/
#   include <stdio.h>
#   include <ctype.h>
#   include <math.h>
#   include <signal.h>
#   include <setjmp.h>
#ifndef _WIN32
#   include <unistd.h>
#endif
#endif

#define CAU_ABS(val) ((val) >= 0 ? (val) : -(val))

/*/subhead CAU_CHAN--------------------------------------------------------
* CAU_CHAN
*
*	A cau channel descriptor contains the data necessary to
*	generate the desired signal and the buffers used for ca_get
*----------------------------------------------------------------------------*/

typedef struct cauSetChannel {
    double	interval;		/* desired interval, in seconds */
    double	jitter;			/* allowed jitter, in seconds */
    struct cauSetChannel *pPrev;	/* link to previous channel */
    struct cauSetChannel *pNext;	/* link to next channel */
    CX_CMD	*pCxCmd;		/* ptr to cmd context, for printing */
    char	name[db_name_dim];	/* channel name (as entered) */
    char	*units;			/* pointer to units, or NULL */
    USHORT	reqCount;		/* requested count, for arrays */
    USHORT	elCount;		/* native count of channel */
    chtype	dbfType;		/* native type of channel */
    chtype	dbrType;		/* desired type for retrieved data */
    chid	pCh;			/* channel pointer */
    evid	pEv;			/* event pointer */
    union db_access_val *pBuf;		/* pointer to buffer */
    union db_access_val *pGRBuf;	/* pointer to graphics info buffer */
    long	(*pFn)();		/* function to call */
    TS_STAMP	lastMonTime;		/* last time handler was called */
    int		lastMonErr;		/* 1 says err msg printed */
    struct {
	short	endVal;			/* end value for signal */
	short	begVal;			/* begin value for signal */
	short	currVal;		/* current value for signal */
	short	addVal;			/* amount to add for next value */
	char	string[db_strval_dim];
    } str;
    struct {
	char	endVal;			/* end value for signal */
	char	begVal;			/* begin value for signal */
	char	currVal;		/* current value for signal */
	char	addVal;			/* amount to add for next value */
    } chr;
    struct {
	float	endVal;			/* end value for signal */
	float	begVal;			/* begin value for signal */
	float	currVal;		/* current value for signal */
	float	addVal;			/* amount to add for next value */
    } flt;
    struct {
	short	endVal;			/* end value for signal */
	short	begVal;			/* begin value for signal */
	short	currVal;		/* current value for signal */
	short	addVal;			/* amount to add for next value */
    } shrt;
    struct {
	double	endVal;			/* end value for signal */
	double	begVal;			/* begin value for signal */
	double	currVal;		/* current value for signal */
	double	addVal;			/* amount to add for next value */
    } dbl;
    struct {
	long	endVal;			/* end value for signal */
	long	begVal;			/* begin value for signal */
	long	currVal;		/* current value for signal */
	long	addVal;			/* amount to add for next value */
    } lng;
    struct {
	short	endVal;			/* end value for signal */
	short	begVal;			/* begin value for signal */
	short	currVal;		/* current value for signal */
	short	addVal;			/* amount to add for next value */
    } enm;
    short	nSteps;			/* number of steps in signal */
    TS_STAMP	nextTime;		/* time for next step in signal */
    double	secPerStep;		/* seconds between steps */
} CAU_CHAN;

/*/subhead CAU_DESC-------------------------------------------------------
* CAU_DESC
*
*	A cau descriptor is the `master handle' which is used for
*	handling channels.  The CAU_DESC is created empty, and then filled
*	in with additional calls.
*----------------------------------------------------------------------------*/
#ifdef vxWorks
#   define CauLock semTake(pglCauDesc->semLock, WAIT_FOREVER)
#   define CauUnlock semGive(pglCauDesc->semLock)
#   define CauLockCheck semClear(pglCauDesc->semLock)
#   define CauLockInitAndLock semInit(pglCauDesc->semLock)
#endif

typedef struct {
#ifdef vxWorks
    TASK_ID	id;		/* ID of task */
#endif
    int		status;		/* status of task--initially ERROR */
    int		stop;		/* task requested to stop if != 0 */
    int		stopped;	/* task has stopped if != 0 */
    int		serviceNeeded;	/* task needs servicing */
    int		serviceDone;	/* task servicing completed */
    jmp_buf	sigEnv;		/* environment for longjmp at signal time */
} CAU_TASK_INFO;

typedef struct cauDesc {
#ifdef vxWorks
    SEM_ID	semLock;
    int		showStack;	/* show stack stats on task terminate */
#endif
    CAU_TASK_INFO cauTaskInfo;
    CAU_TASK_INFO cauInTaskInfo;
    CX_CMD	*pCxCmd;	/* pointer to command context */
    CAU_CHAN	*pChanHead;	/* pointer to head of channel list */
    CAU_CHAN	*pChanTail;	/* pointer to tail of channel list */
    CAU_CHAN	*pChanConnHead;	/* pointer to head of channel connect list */
    CAU_CHAN	*pChanConnTail;	/* pointer to tail of channel connect list */
    double	secPerStep;	/* seconds per step for signal generation */
    int		nSteps;		/* number of steps per cycle for sig gen */
    double	begVal;		/* begin value for generated signal */
    double	endVal;		/* end value for generated signal */
} CAU_DESC;

/*-----------------------------------------------------------------------------
* prototypes
*----------------------------------------------------------------------------*/
int cau();
static void cauCaException();
static void cauCmdProcess();
static long cauTask();
#ifdef vxWorks
static long cauTaskCheck();
static long cauInit();
#endif
static void cauTaskSigHandler();
static char *cauInTask();
static void cau_deadband();
static void cau_debug();
static void cau_delete();
static void cau_get();
static void cau_info();
static void cau_interval(), cau_interval_deadTime_test();
static void cau_monitor();
static void cau_put();
static void cau_ramp();

static CAU_CHAN * cauChanAdd();
static long cauChanDel();
static CAU_CHAN *cauChanFind();
static long cauFree();
static void cauGetAndPrint();
static void cauInitAtStartup();
static void cauMonitor();
static void cauPrintBuf();
static void cauPrintBufArray();
static void cauPrintInfo();
static void cauSigGen();
static long cauSigGenGetParams();
static long cauSigGenPut();
static long cauSigGenRamp();
static void cauSigGenRampAdd();

/*-----------------------------------------------------------------------------
* global definitions
*----------------------------------------------------------------------------*/
static CX_CMD		glCauCxCmd;
static CX_CMD		*pglCauCxCmd=NULL;
static CAU_DESC		glCauDesc;
static CAU_DESC		*pglCauDesc=NULL;
static int		glCauDebug=0;

static HELP_TOPIC	helpDebug;	/* help info--debug command */
static HELP_TOPIC	helpInterval;	/* help info--interval command */
static HELP_TOPIC	helpRamp;	/* help info--ramp command */
static unsigned long glCauDeadband=DBE_VALUE | DBE_ALARM;
static char	*glCauMDEL_msg="prior to ca_add_masked_array_event (MDEL)";
static char	*glCauADEL_msg="prior to ca_add_masked_array_event (ADEL)";



/*+/subr**********************************************************************
* NAME	cauCaDebug...
*-*/
epicsTimeStamp cauDbStamp;
static char cauDbStampTxt[28];

static void cauCaDebug(message, invokeVal)
char	*message;
int	invokeVal;
{
    if (glCauDebug <= invokeVal)
	return;
    (void)epicsTimeGetCurrent(&cauDbStamp);
    (void)epicsTimeToStrftime(cauDbStampTxt,28,"%m-%d-%y %H:%M:%S.%09f",&cauDbStamp);
    (void)printf("%s %s\n", &cauDbStampTxt[12], message);
}

static void cauCaDebugDbrAndName(message, type, name, invokeVal)
char	*message;
chtype	type;
char	*name;
int	invokeVal;
{
    if (glCauDebug <= invokeVal)
	return;
    (void)epicsTimeGetCurrent(&cauDbStamp);
    (void)epicsTimeToStrftime(cauDbStampTxt,28,"%m-%d-%y %H:%M:%S.%09f",&cauDbStamp);
    (void)printf("%s %s (%s) for %s\n",
		&cauDbStampTxt[12], message, dbr_type_to_text(type), name);
}

static void cauCaDebugName(message, name, invokeVal)
char	*message;
char	*name;
int	invokeVal;
{
    if (glCauDebug <= invokeVal)
	return;
    (void)epicsTimeGetCurrent(&cauDbStamp);
    (void)epicsTimeToStrftime(cauDbStampTxt,28,"%m-%d-%y %H:%M:%S.%09f",&cauDbStamp);
    (void)printf("%s %s for %s\n", &cauDbStampTxt[12], message, name);
}

static void cauCaDebugStat(message, stat, invokeVal)
char	*message;
long	stat;
int	invokeVal;
{
    if (glCauDebug <= invokeVal)
	return;
    (void)epicsTimeGetCurrent(&cauDbStamp);
    (void)epicsTimeToStrftime(cauDbStampTxt,28,"%m-%d-%y %H:%M:%S.%09f",&cauDbStamp);
    (void)printf("%s %s %s\n", &cauDbStampTxt[12], message, ca_message(stat));
}

#ifndef vxWorks
    int main()
    {
	return cau();
    }
#endif

/*+/subr**********************************************************************
* NAME	cau - shell callable interface for cau
*
* DESCRIPTION
*	This routine is the only part of cau which is intended to be
*	called directly from the shell.  Several functions are performed here:
*	o   if the cauTask doesn't exist, spawn it.  Ideally, this
*	    stage would also detect if the cauTask is suspended
*	    and take appropriate action.
*	o   if the cauInTask doesn't exist, spawn it.  If it does exist, an
*	    error exists.
*	o   wait until the cauInTask quits, then return to the shell.  If
*	    other tasks belonging to cau are being stopped, then this
*	    routine waits until they, too, are stopped before returning to
*	    the shell.
*
* RETURNS
*	OK, or
*	ERROR
*
* BUGS
* o	stack size and priority should come from tasks.h
* o	there are lots of "holes" in detecting whether tasks exist, are
*	suspended, etc.
*
*-*/
int
cau()
{

    if (pglCauCxCmd == NULL ||
		(pglCauDesc->cauTaskInfo.stop == 1 &&
		 pglCauDesc->cauInTaskInfo.stop == 1)   ) {
	glCauDebug = 0;
	pglCauDesc = &glCauDesc;
	pglCauCxCmd = &glCauCxCmd;
	cauInitAtStartup(pglCauDesc, pglCauCxCmd);

#ifdef vxWorks
	assert(taskNameToId("cauTask") == ERROR);
	assert(taskNameToId("cauInTask") == ERROR);
	pglCauDesc->showStack = 0;
#endif

	pglCauDesc->cauTaskInfo.status = ERROR;
	pglCauDesc->cauTaskInfo.stop = 0;
	pglCauDesc->cauTaskInfo.stopped = 1;

	pglCauDesc->cauInTaskInfo.status = ERROR;
	pglCauDesc->cauInTaskInfo.stop = 0;
	pglCauDesc->cauInTaskInfo.stopped = 1;
    }
    pglCauDesc->cauInTaskInfo.serviceNeeded = 0;
    pglCauDesc->cauInTaskInfo.serviceDone = 1;

#ifdef vxWorks
/*-----------------------------------------------------------------------------
* cauTask
*	check its status; spawn if it doesn't exist
*----------------------------------------------------------------------------*/
    stat = cauTaskCheck(pglCauCxCmd, "cauTask", &pglCauDesc->cauTaskInfo);
    if (stat == ERROR)
	return ERROR;
    if (pglCauDesc->cauTaskInfo.status == ERROR) {
	pglCauDesc->cauTaskInfo.id = taskSpawn("cauTask", MAXPRIO,
		VX_STDIO | VX_FP_TASK, 50000, cauTask, &pglCauCxCmd);
    }
    if (GenTaskNull(pglCauDesc->cauTaskInfo.id)) {
	(void)printf("error spawning cauTask\n");
	return ERROR;
    }
    pglCauDesc->cauTaskInfo.status = OK;
    pglCauDesc->cauTaskInfo.stopped = 0;

/*-----------------------------------------------------------------------------
* cauInTask
*	check its status; spawn if it doesn't exist
*----------------------------------------------------------------------------*/
    stat = cauTaskCheck(pglCauCxCmd, "cauInTask", &pglCauDesc->cauInTaskInfo);
    if (stat == ERROR)
	return ERROR;
    if (pglCauDesc->cauInTaskInfo.status == ERROR) {
	pglCauDesc->cauInTaskInfo.id = taskSpawn("cauInTask", MAXPRIO,
		VX_STDIO | VX_FP_TASK, 50000, cauInTask, &pglCauCxCmd);
    }
    if (GenTaskNull(pglCauDesc->cauInTaskInfo.id)) {
	(void)printf("error spawning cauInTask\n");
	return ERROR;
    }
    pglCauDesc->cauInTaskInfo.status = OK;
    pglCauDesc->cauInTaskInfo.stopped = 0;

/*-----------------------------------------------------------------------------
*    wait for cauInTask to exit and then return to the shell.  If other
*    "cau tasks" are also exiting, wait until their wrapups are complete.
*----------------------------------------------------------------------------*/
    while (!pglCauDesc->cauInTaskInfo.stopped)
	taskSleep(SELF, 1, 0);
    if (pglCauDesc->cauTaskInfo.stop) {
	while (!pglCauDesc->cauTaskInfo.stopped)
	    taskSleep(SELF, 1, 0);
    }
#else
    cauTask(&pglCauCxCmd);
#endif

    return OK;
}

static void
cauCaException(arg)
struct exception_handler_args arg;
{
    chid	pCh;
    int		stat;

    pCh = arg.chid;
    stat = arg.stat;
    (void)printf("CA exception handler entered\n");
    if(pCh){
      (void)printf("CA channel name=%s\n", ca_name(pCh));
    }
    (void)printf("CA status=%s\n", ca_message(stat));
    (void)printf("CA context=%s\n", arg.ctx);
    (void)printf("CA op=%ld data type=%s count=%ld\n",
                arg.op,
                dbr_type_to_text(arg.type),
                arg.count);
}
/*+/subr**********************************************************************
* NAME	cauTask - main processing task for cau
*
* DESCRIPTION
*
* RETURNS
*	OK, or
*	ERROR
*
* BUGS
* o	text
*
*-*/
static long
cauTask(ppCxCmd)
CX_CMD	**ppCxCmd;
{
    long	stat;
    CX_CMD	*pCxCmd;
    int		loopCount;
    int		sigNum;

    pCxCmd = *ppCxCmd;

#ifdef vxWorks
    CauLockInitAndLock;
    CauUnlock;
#endif

    genSigInit(cauTaskSigHandler);
    if ((sigNum = setjmp(pglCauDesc->cauTaskInfo.sigEnv)) != 0) {
	printf("cau: signal detected: %d\n", sigNum);
	goto cauTaskWrapup;
    }

    stat = ca_task_initialize();
    assert(stat == ECA_NORMAL);
    stat = ca_add_exception_event(cauCaException, NULL);
    assert(stat == ECA_NORMAL);

/*----------------------------------------------------------------------------
*    "processing loop"
*---------------------------------------------------------------------------*/
    loopCount = 10;
    while (!pglCauDesc->cauTaskInfo.stop) {
	cauCaDebug("main loop, prior to ca_pend_event(0.001)", 2);
	stat = ca_pend_event(0.001);
	cauCaDebugStat("main loop, back from ca_pend_event(0.001)", stat, 2);
	assert(stat != ECA_EVDISALLOW);
	cauSigGen(pCxCmd, pglCauDesc);
	if (--loopCount <= 0) {
	    loopCount = 10;
	    cau_interval_deadTime_test(pglCauDesc);
	}
#ifndef vxWorks
	cauInTask(ppCxCmd);
#endif
	if (pglCauDesc->cauInTaskInfo.serviceNeeded) {
	    cauCmdProcess(ppCxCmd, pglCauDesc);
	    pCxCmd = *ppCxCmd;
	    pglCauDesc->cauInTaskInfo.serviceNeeded = 0;
	    pglCauDesc->cauInTaskInfo.serviceDone = 1;
	}
#ifdef vxWorks		/* fprintf on vxWorks not flushed on \n */
	fflush(stdout);
	fflush(pCxCmd->dataOut);
	fflush(stderr);
	taskSleep(SELF, 0, 100000);	/* wait .1 sec */
#else
	sleep(1);
/* MDA - usleep isn't POSIX
	usleep(100000);
*/
#endif
    }

cauTaskWrapup:
    stat = cauFree(pCxCmd, pglCauDesc);
    assert(stat == OK);

    while ((*ppCxCmd)->pPrev != NULL)
	cmdCloseContext(ppCxCmd);
    pCxCmd = *ppCxCmd;

    stat = ca_task_exit();
    if (stat != ECA_NORMAL) {
	(void)printf("cau: ca_task_exit error: %s\n", ca_message(stat));
    }

    pglCauDesc->cauInTaskInfo.stop = 1;
#ifdef vxWorks
    while (pglCauDesc->cauInTaskInfo.stopped == 0) {
	taskSleep(SELF, 1, 0);
    }
#endif

    if (pCxCmd->dataOutRedir) {
	(void)printf("closing dataOut\n");
	fclose(pCxCmd->dataOut);
    }

#ifdef vxWorks
    if (pglCauDesc->showStack)
	checkStack(pglCauDesc->cauTaskInfo.id);
#endif

    pglCauDesc->cauTaskInfo.stopped = 1;
    pglCauDesc->cauTaskInfo.status = ERROR;

    return 0;
}

/*+/subr**********************************************************************
* NAME	cauTaskCheck - check on a task
*
* DESCRIPTION
*	Check to see if a task exists (based on its name).  If it does:
*	o   verify that its status in the CAU_TASK_INFO block is OK
*	o   verify that it isn't suspended
*
*	If the task doesn't exist:
*	o   verify that its status in the CAU_TASK_INFO block is ERROR
*
* RETURNS
*	OK, or
*	ERROR if an inconsistency is found
*
* BUGS
* o	This isn't a general purpose routine.  In particular, under SunOS,
*	the assumption is that the task doesn't exist.
*
*-*/
#ifdef vxWorks
static long
cauTaskCheck(pCxCmd, name, pTaskInfo)
CX_CMD	*pCxCmd;	/* I pointer to command context */
char	*name;		/* I name of task */
CAU_TASK_INFO *pTaskInfo;/* IO pointer to task info block */
{
    if (taskNameToId(name) == ERROR) {
	assert(pTaskInfo->status == ERROR);
	pTaskInfo->stop = 0;
	pTaskInfo->stopped = 0;
    }
    else {
	assert(pTaskInfo->status == OK);
	assert(!GenTaskNull(pTaskInfo->id));
	if (taskIsSuspended(pTaskInfo->id)) {
	    (void)printf("%s is suspended\n", name);
	    return ERROR;
	}
    }
    return OK;
}
#endif

/*+/subr**********************************************************************
* NAME	cauTaskSig - signal handling and initialization
*
* DESCRIPTION
*	These routines set up for the signals to be caught for cauTask
*	and handle the signals when they occur.
*
* RETURNS
*	void
*
* BUGS
* o	not all signals are caught
* o	under VxWorks, taskDeleteHookAdd isn't used
* o	it's not clear how useful it is to catch the signals which originate
*	from the keyboard
*
*-*/
static void
cauTaskSigHandler(signo)
int	signo;
{
    signal(signo, SIG_DFL);
    longjmp(pglCauDesc->cauTaskInfo.sigEnv, 1);
}

/*+/subr**********************************************************************
* NAME	cauInTask - handle the keyboard and keyboard commands
*
* DESCRIPTION
*	Gets input text and passes the input to cauTask.
*
*	This task exists for two primary purposes: 1) avoid the possibility
*	of blocking cauTask while waiting for operator input; and
*	2) allow detaching cauTask from the keyboard while still
*	allowing cauTask to run.
*
*	It is important to note that this task does no command processing
*	on cauTask's behalf--all commands relating to cauTask
*	are executed in cauTask's own context.  Some
*	commands are related to getting input--these commands are processed
*	by this task and are never visible to cauTask.
*
*	This task waits for input to be available from the keyboard.  When
*	an input line is ready, this task does some preliminary processing:
*	o   if the command is "quit" (or ^D), cauTask is signalled
*	    and this task wraps itself up and returns.
*	o   if the command is "dataOut", this task sets a new destination
*	    for data output, closing the previous destination, if appropriate,
*	    and opening the new destination, if appropriate.
*	o   if the command is "bg", this task wraps itself up and returns.
*	    A flag is left in the command context indicating that cauInTask
*	    doesn't exist any more.
*	o   if the command is "source", this task pushes down a level in
*	    the command context and begins reading commands from the file.
*	    When EOF occurs on the file, cauInTask closes the file, pops
*	    to the previous level in the command context, and resumes
*	    reading at that level.
*	o   otherwise, cauTask is signalled and this task
*	    goes into a sleeping loop until cauTask signals
*	    that it is ready for the next command.
*
*	Ideally, this task would also support a mechanism for cauTask
*	to wait explicitly for keyboard input.  An example would be when
*	operator permission is needed prior to taking an action.
*
* RETURNS
*	OK, or
*	ERROR
*
* BUGS
* o	doesn't support socket I/O
* o	doesn't support flexible redirection of output
* o	the implementation of "dataOut" is clumsy
*
*-*/
static char *
cauInTask(ppCxCmd)
CX_CMD	**ppCxCmd;	/* IO ptr to pointer to command context */
{
    char	*input = NULL;
/*----------------------------------------------------------------------------
*    wait for input from keyboard.  When some is received, signal caller,
*    wait for caller to process it, and then wait for some more input.
*
*    stay in the main loop until get ^D or 'quit'
*---------------------------------------------------------------------------*/
    while (1) {
#ifdef vxWorks
	while (pglCauDesc->cauInTaskInfo.serviceDone == 0) {
	    if (pglCauDesc->cauInTaskInfo.stop == 1)
		goto cauInTaskDone;
	    taskSleep(SELF, 0, 500000);		/* sleep .5 sec */
	}
#endif

	input = cmdRead(ppCxCmd);

#ifdef vxWorks
	if (pglCauDesc->cauInTaskInfo.stop == 1)
	    goto cauInTaskDone;
#endif
	if (input == NULL)
#ifdef vxWorks
	    ;
#else
	    goto cauInTaskDone;
#endif
	else if (strcmp((*ppCxCmd)->pCommand,		"dataOut") == 0) {
	    if ((*ppCxCmd)->dataOutRedir)
		fclose ((*ppCxCmd)->dataOut);
	    (*ppCxCmd)->dataOutRedir = 0;
	    if (nextNonSpaceField( &(*ppCxCmd)->pLine, &(*ppCxCmd)->pField,
							&(*ppCxCmd)->delim) < 1)
		(*ppCxCmd)->dataOut = stdout;
	    else {
		(*ppCxCmd)->dataOut = fopen((*ppCxCmd)->pField, "a");
		if ((*ppCxCmd)->dataOut == NULL) {
		    (void)printf("couldn't open %s\n", (*ppCxCmd)->pField);
		    (*ppCxCmd)->dataOut = stdout;
		}
		else
		    (*ppCxCmd)->dataOutRedir = 1;
	    }
	}
#ifdef vxWorks
	else if (strcmp((*ppCxCmd)->pCommand,		"bg") == 0) {
	    if (cmdBgCheck(*ppCxCmd) == OK)
		goto cauInTaskDone;
	}
	else if (strcmp((*ppCxCmd)->pCommand,		"quit") == 0) {
	    pglCauDesc->cauInTaskInfo.serviceDone = 0;
	    pglCauDesc->cauInTaskInfo.serviceNeeded = 1;
	    goto cauInTaskDone;
	}
#endif
	else if (strcmp((*ppCxCmd)->pCommand,		"source") == 0) {
	    cmdSource(ppCxCmd);
	}
	else {
	    pglCauDesc->cauInTaskInfo.serviceDone = 0;
	    pglCauDesc->cauInTaskInfo.serviceNeeded = 1;
#ifndef vxWorks
	    goto cauInTaskDone;
#endif
	}
    }

cauInTaskDone:
#ifdef vxWorks
    while ((*ppCxCmd)->pPrev != NULL)
	cmdCloseContext(ppCxCmd);

    if (pglCauDesc->showStack)
	checkStack(pglCauDesc->cauInTaskInfo.id);
    pglCauDesc->cauInTaskInfo.stop = 1;
    pglCauDesc->cauInTaskInfo.stopped = 1;
    pglCauDesc->cauInTaskInfo.status = ERROR;
#endif

    return input;
}

/*+/subr**********************************************************************
* NAME	cauCmdProcess - process a command line
*
* DESCRIPTION
*
* RETURNS
*	void
*
* BUGS
* o	text
*
*-*/
static void
cauCmdProcess(ppCxCmd, pCauDesc)
CX_CMD	**ppCxCmd;	/* IO ptr to pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    CX_CMD	*pCxCmd;	/* local copy of pointer, for convenience */

    pCxCmd = *ppCxCmd;
    if (strcmp(pCxCmd->pCommand,			"assert0") == 0) {
/*-----------------------------------------------------------------------------
*	under SunOS, this generates SIGABRT; for VxWorks, SIGUSR1
*----------------------------------------------------------------------------*/
	assertAlways(0);
    }
    else if (strcmp(pCxCmd->pCommand,			"quit") == 0) {
	/* insert wrapup processing here */
	pCauDesc->cauTaskInfo.stop = 1;
    }
#ifdef vxWorks
    else if (strcmp(pCxCmd->pCommand,			"showStack") == 0)
	pCauDesc->showStack = 1;
#endif
    else if (strcmp(pCxCmd->pCommand,			"trap") == 0) {
	int	*j;
	j = (int *)(-1);
	j = (int *)(*j);
    }
    else if (strcmp(pCxCmd->pCommand,			"deadband") == 0)
	cau_deadband(pCxCmd);
    else if (strcmp(pCxCmd->pCommand,			"debug") == 0)
	cau_debug(pCxCmd);
    else if (strcmp(pCxCmd->pCommand,			"delete") == 0) {
	if (pCauDesc->pChanHead == NULL) goto noChanErr;
	cau_delete(pCxCmd, pCauDesc);
    }
    else if (strcmp(pCxCmd->pCommand,			"get") == 0)
	cau_get(pCxCmd, pCauDesc);
    else if (strcmp(pCxCmd->pCommand,			"info") == 0)
	cau_info(pCxCmd, pCauDesc);
    else if (strcmp(pCxCmd->pCommand,			"interval") == 0)
	cau_interval(pCxCmd, pCauDesc);
    else if (strcmp(pCxCmd->pCommand,			"monitor") == 0)
	cau_monitor(pCxCmd, pCauDesc);
    else if (strcmp(pCxCmd->pCommand,			"put") == 0)
	cau_put(pCxCmd, pCauDesc);
    else if (strcmp(pCxCmd->pCommand,			"ramp") == 0)
	cau_ramp(pCxCmd, pCauDesc);
    else {
/*----------------------------------------------------------------------------
* help (or illegal command)
*----------------------------------------------------------------------------*/
	(void)nextNonSpaceField(&pCxCmd->pLine, &pCxCmd->pField,
							&pCxCmd->delim);
	helpIllegalCommand(stdout, &pCxCmd->helpList, pCxCmd->pCommand,
							pCxCmd->pField);
    }

    goto cmdDone;
noChanErr:
    (void)printf("no channels selected\n");
cmdDone:
    ;
    return;
}

/*+/subr**********************************************************************
* NAME	cau_deadband
*-*/
static void
cau_deadband(pCxCmd)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
{
    char	*opt;

    if (nextNonSpaceField(&pCxCmd->pLine, &opt, &pCxCmd->delim) <= 1) {
	(void)printf("you must specify either MDEL or ADEL\n");
	return;
    }
    if (strcmp(opt, "MDEL") == 0)
	glCauDeadband = DBE_VALUE | DBE_ALARM;
    else if (strcmp(opt, "ADEL") == 0)
	glCauDeadband = DBE_LOG | DBE_ALARM;
    else
	(void)printf("you must specify either MDEL or ADEL\n");
}

/*+/subr**********************************************************************
* NAME	cau_debug
*-*/
static void
cau_debug(pCxCmd)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
{
    int		i;

    if (nextIntFieldAsInt(&pCxCmd->pLine, &i, &pCxCmd->delim) > 1)
	glCauDebug = i;
    else if (pCxCmd->delim != '-')
	glCauDebug++;
    else
	glCauDebug--;
}

/*+/subr**********************************************************************
* NAME	cau_delete
*-*/
static void
cau_delete(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    CAU_CHAN	*pChanNext;	/* temp for channel pointer */

    pCxCmd->fldLen =
	nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1) {
	(void)printf(
	    "you must specify one or more channels  or else specify all\n");
	return;
    }
    if (strcmp(pCxCmd->pField, "all") == 0) {
	pChan = pCauDesc->pChanHead;
	while (pChan != NULL) {
	    pChanNext = pChan->pNext;
	    if (cauChanDel(pCxCmd, pCauDesc, pChan) != OK) {
		(void)printf("error deleting %s \n", pCxCmd->pField);
	    }
	    pChan = pChanNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL)
	    (void)printf("%s not selected\n", pCxCmd->pField);
	else {
	    if (cauChanDel(pCxCmd, pCauDesc, pChan) != OK) {
		(void)printf("error deleting %s \n", pCxCmd->pField);
	    }
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*+/subr**********************************************************************
* NAME	cau_get
*-*/
static void
cau_get(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    int		count=-1;

    if (pCxCmd->delim == ',') {
	nextIntFieldAsInt(&pCxCmd->pLine, &count, &pCxCmd->delim);
	if (count <= 0) {
	    (void)printf("error in count field\n");
	    return;
	}
    }
    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
	    if (count > 0) {
		if (count <= (int)pChan->elCount)
		    pChan->reqCount = count;
		else
		    pChan->reqCount = pChan->elCount;
	    }
	    cauGetAndPrint(pCxCmd, pCauDesc, pChan, 1, 0, 0);
	    pChan = pChan->pNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	    pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
	    if (pChan == NULL)
		(void)printf("couldn't open %s \n", pCxCmd->pField);
	}
	if (pChan != NULL) {
	    pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
	    if (count > 0) {
		if (count <= (int)pChan->elCount)
		    pChan->reqCount = count;
		else
		    pChan->reqCount = pChan->elCount;
	    }
	    cauGetAndPrint(pCxCmd, pCauDesc, pChan, 1, 0, 0);
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*+/subr**********************************************************************
* NAME	cau_info
*-*/
static void
cau_info(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    CAU_CHAN	*pChan;		/* temp for channel pointer */

    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    cauPrintInfo(pCxCmd, pChan);
	    pChan = pChan->pNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	    pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
	    if (pChan == NULL) {
		(void)printf("couldn't open %s \n", pCxCmd->pField);
	    }
	}
	if (pChan != NULL)
	    cauPrintInfo(pCxCmd, pChan);
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*+/subr**********************************************************************
* NAME	cau_interval
*	interval,sec[,jitter] [chanName [chanName ... ]]
*-*/
static void
cau_interval(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	stat;
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    int		stopFlag;	/* 1 indicates to stop an activity */
    double	interval;	/* desired interval between samples, or 0. */
    double	jitter;		/* allowed jitter in interval */
    char	*msg;

    if (glCauDeadband & DBE_VALUE)
	msg = glCauMDEL_msg;
    else
	msg = glCauADEL_msg;

    if (pCxCmd->delim == '-')
	stopFlag = 1;
    else {
	stopFlag = 0;
	if (pCxCmd->delim != ',') {
	    (void)printf("you must specify an interval, in seconds\n");
	    return;
	}
	if (nextFltFieldAsDbl(&pCxCmd->pLine, &interval, &pCxCmd->delim) <= 1 ||
							interval <= 0.) {
	    (void)printf("illegal interval\n");
	    return;
	}
	if (pCxCmd->delim == ',') {
	    if (nextFltFieldAsDbl(&pCxCmd->pLine,&jitter,&pCxCmd->delim)<= 1 ||
							    jitter <= 0.) {
		(void)printf("illegal jitter\n");
		return;
	    }
	}
	else
	    jitter = .0015;
    }

    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
	    pChan->interval = 0.;
	    pChan->jitter = 0.;
	    if (pChan->pEv != NULL) {
		cauCaDebugName("prior to ca_clear_event", pChan->name, 0);
		stat = ca_clear_event(pChan->pEv);
		cauCaDebugStat("back from ca_clear_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_clear_event error: %s\n", pCxCmd->pField);
		}
		pChan->pEv = NULL;
	    }
	    if (!stopFlag) {
		pChan->interval = interval;
		pChan->jitter = jitter;
		pChan->lastMonTime.secPastEpoch = 0;
		pChan->lastMonErr = 0;
		if (pChan->pEv == NULL) {
		    cauCaDebugDbrAndName(msg, pChan->dbrType, pChan->name, 0);
		    stat = ca_add_masked_array_event(pChan->dbrType,
				pChan->reqCount, pChan->pCh, cauMonitor, pChan,
				0., 0., 0., &pChan->pEv, glCauDeadband);
		    cauCaDebugStat("back from ca_add_array_event", stat, 0);
		    if (stat != ECA_NORMAL) {
			(void)printf("ca_add_event error:%s\n",pCxCmd->pField);
			pChan->pEv = NULL;
		    }
		}
	    }
	    pChan = pChan->pNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	    if (stopFlag)
		(void)printf("couldn't find %s \n", pCxCmd->pField);
	    else {
		pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
		if (pChan == NULL) {
		    (void)printf("couldn't open %s\n", pCxCmd->pField);
		}
	    }
	}
	if (pChan != NULL) {
	    pChan->interval = 0.;
	    pChan->jitter = 0.;
	    if (pChan->pEv != NULL) {
		cauCaDebugName("prior to ca_clear_event", pChan->name, 0);
		stat = ca_clear_event(pChan->pEv);
		cauCaDebugStat("back from ca_clear_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_clear_event error: %s\n", pCxCmd->pField);
		}
		pChan->pEv = NULL;
	    }
	    if (!stopFlag) {
		pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
		pChan->interval = interval;
		pChan->jitter = jitter;
		pChan->lastMonTime.secPastEpoch = 0;
		pChan->lastMonErr = 0;
		if (pChan->pEv == NULL) {
		    cauCaDebugDbrAndName(msg, pChan->dbrType, pChan->name, 0);
		    stat = ca_add_masked_array_event(pChan->dbrType,
				pChan->reqCount, pChan->pCh, cauMonitor, pChan,
				0., 0., 0., &pChan->pEv, glCauDeadband);
		    cauCaDebugStat("back from ca_add_array_event", stat, 0);
		    if (stat != ECA_NORMAL) {
			(void)printf("ca_add_event error:%s\n",pCxCmd->pField);
			pChan->pEv = NULL;
		    }
		}
	    }
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}
/*+/subr**********************************************************************
* NAME	cau_interval_deadTime_test
*-*/
static void
cau_interval_deadTime_test(pCauDesc)
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    epicsTimeStamp	now;
    char	nowText[28];
    char	lastMonText[28];
    char	chanTsText[28];
    CAU_CHAN	*pChan;
    double	deadTime;

    (void)epicsTimeGetCurrent(&now);
    (void)epicsTimeToStrftime(nowText,28,"%m-%d-%y %H:%M:%S.%09f",&now);
    pChan = pglCauDesc->pChanHead;
    while (pChan != NULL) {
	if (pChan->interval > 0. && pChan->lastMonErr == 0 &&
		    			pChan->lastMonTime.secPastEpoch > 0) {
	    deadTime = epicsTimeDiffInSeconds(&now, &pChan->lastMonTime);  /* left - right */
	    if (deadTime > pChan->interval + 1.) {
		pChan->lastMonErr = 1;
		(void)fprintf(pCauDesc->pCxCmd->dataOut,
				"deadTime viol. %s at %s (local)\n",
				pChan->name, nowText);

		(void)epicsTimeToStrftime(lastMonText,28,"%m-%d-%y %H:%M:%S.%09f",
                                                &pChan->lastMonTime);
		(void)epicsTimeToStrftime(chanTsText,28,"%m-%d-%y %H:%M:%S.%09f",
                                                &pChan->pBuf->tstrval.stamp);
		(void)fprintf(pCauDesc->pCxCmd->dataOut,
		    "last mon at %s (local) or %s (ioc)\n", lastMonText, chanTsText);
		if (pCauDesc->pCxCmd->dataOut != pCauDesc->pCxCmd->dataOut) {
		    (void)fprintf(pCauDesc->pCxCmd->dataOut,
				"deadTime viol. %s at %s (local)\n",
				pChan->name, nowText);
		    (void)fprintf(pCauDesc->pCxCmd->dataOut,
			"last mon at %s (local) or %s (ioc)\n",
						lastMonText, chanTsText);
		}
	    }
	}
	pChan = pChan->pNext;
    }
}

/*+/subr**********************************************************************
* NAME	cau_monitor
*-*/
static void
cau_monitor(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	stat;
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    int		stopFlag;	/* 1 indicates to stop an activity */
    int		count=-1;
    char	*msg;

    if (pCxCmd->delim == ',') {
	nextIntFieldAsInt(&pCxCmd->pLine, &count, &pCxCmd->delim);
	if (count <= 0) {
	    (void)printf("error in count field\n");
	    return;
	}
    }

    if (pCxCmd->delim == '-')
	stopFlag = 1;
    else
	stopFlag = 0;

    if (glCauDeadband & DBE_VALUE)
	msg = glCauMDEL_msg;
    else
	msg = glCauADEL_msg;

    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
	    if (count > 0) {
		if (count <= (int)pChan->elCount)
		    pChan->reqCount = count;
		else
		    pChan->reqCount = pChan->elCount;
	    }
	    pChan->interval = 0.;
	    pChan->lastMonErr = 0;
	    if (pChan->pEv != NULL) {
		cauCaDebugName("prior to ca_clear_event", pChan->name, 0);
		stat = ca_clear_event(pChan->pEv);
		cauCaDebugStat("back from ca_clear_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_clear_event error: %s \n", pCxCmd->pField);
		}
		pChan->pEv = NULL;
	    }
	    if (!stopFlag) {
		cauCaDebugDbrAndName(msg, pChan->dbrType, pChan->name, 0);
		stat = ca_add_masked_array_event(pChan->dbrType,
				pChan->reqCount, pChan->pCh, cauMonitor, pChan,
				0., 0., 0., &pChan->pEv, glCauDeadband);
		cauCaDebugStat("back from ca_add_array_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_add_event error:%s\n",pCxCmd->pField);
		    pChan->pEv = NULL;
		}
	    }
	    pChan = pChan->pNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	    if (stopFlag) {
		(void)printf("couldn't find %s \n", pCxCmd->pField);
	    }
	    else {
		pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
		if (pChan == NULL) {
		    (void)printf("couldn't open %s \n", pCxCmd->pField);
		}
	    }
	}
	if (pChan != NULL) {
	    if (pChan->pEv != NULL) {
		cauCaDebugName("prior to ca_clear_event", pChan->name, 0);
		stat = ca_clear_event(pChan->pEv);
		cauCaDebugStat("back from ca_clear_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_clear_event error: %s \n", pCxCmd->pField);
		}
		pChan->pEv = NULL;
	    }
	    if (!stopFlag) {
		pChan->interval = 0.;
		pChan->lastMonErr = 0;
		pChan->dbrType = dbf_type_to_DBR_TIME(pChan->dbfType);
		if (count > 0) {
		    if (count <= (int)pChan->elCount)
			pChan->reqCount = count;
		    else
			pChan->reqCount = pChan->elCount;
		}
		cauCaDebugDbrAndName(msg, pChan->dbrType, pChan->name, 0);
		stat = ca_add_masked_array_event(pChan->dbrType,
				pChan->reqCount, pChan->pCh, cauMonitor, pChan,
				0., 0., 0., &pChan->pEv, glCauDeadband);
		cauCaDebugStat("back from ca_add_array_event", stat, 0);
		if (stat != ECA_NORMAL) {
		    (void)printf("ca_add_event error: %s \n", pCxCmd->pField);
		    pChan->pEv = NULL;
		}
	    }
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*+/subr**********************************************************************
* NAME	cau_put
*	put chanName value
*-*/
static void
cau_put(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	stat;
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    char	*pValue;	/* temp for value pointer */
    int		i;

    if ((pCxCmd->fldLen = nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField,
							&pCxCmd->delim)) <= 1)
	goto putError;
    if ((pCxCmd->fldLen = nextNonSpaceField(&pCxCmd->pLine, &pValue,
							&pCxCmd->delim)) <= 1)
	goto putError;
    if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
	if (pChan == NULL) {
	    (void)printf("couldn't open %s \n", pCxCmd->pField);
	    return;
	}
    }
#if 0
    if (pChan->elCount != 1) {
	(void)printf("can't put for arrays or waveforms\n");
	return;
    }
#endif
    if (pChan->dbfType == DBF_ENUM) {
	if (pChan->pGRBuf->genmval.status == -2) {
	    (void)printf("can't do put--no graphics info for channel\n");
	    return;
	}
	i = 0;
	while (1) {
	    if (strcmp(pValue, pChan->pGRBuf->genmval.strs[i]) == 0)
		break;
	    i++;
	    if (i >= pChan->pGRBuf->genmval.no_str) {
		(void)printf("bad state string; legal state strings are:\n");
		for (i=0; i<pChan->pGRBuf->genmval.no_str; i++)
	            (void)printf("\"%s\"  ", pChan->pGRBuf->genmval.strs[i]);
		(void)printf("\n");
		(void)printf(
"(It is necessary to use \" only if string contains blanks.)\n");
		return;
	    }
	}
    }
    cauCaDebug("prior to ca_put (DBR_STRING)", 0);
    stat = ca_put(DBR_STRING, pChan->pCh, pValue);
    cauCaDebugStat("back from ca_put", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error on ca_put\n");
	return;
    }
    cauCaDebug("prior to ca_pend_io(1.0)", 0);
    stat = ca_pend_io(1.0);
    cauCaDebugStat("back from ca_pend_io(1.0)", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error on ca_put\n");
	return;
    }
    return;
putError:
	(void)printf("you must specify a channel and a value\n");
}

/*+/subr**********************************************************************
* NAME	cau_ramp
*	ramp[,[secPerStep],[nSteps],[begVal],[endVal]] chanName [chanName ...]
*-*/
static void
cau_ramp(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    int		stopFlag;	/* 1 indicates to stop an activity */

    if (pCxCmd->delim == '-')
	stopFlag = 1;
    else {
	stopFlag = 0;
	if (cauSigGenGetParams(pCxCmd, pCauDesc) != OK)
	    return;
    }

    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    if (stopFlag) {
		if (pChan->pFn == cauSigGenRamp)
		    pChan->pFn = NULL;
	    }
	    else
		cauSigGenRampAdd(pCxCmd, pCauDesc, pChan);
	    pChan = pChan->pNext;
	}
	return;
    }
    while (pCxCmd->fldLen > 1) {
	if ((pChan = cauChanFind(pCauDesc, pCxCmd->pField)) == NULL) {
	    if (stopFlag) {
		(void)printf("couldn't find %s \n", pCxCmd->pField);
	    }
	    else
		pChan = cauChanAdd(pCxCmd, pCauDesc, pCxCmd->pField);
	    if (pChan == NULL)
		(void)printf("couldn't open %s \n",pCxCmd->pField);
	}
	if (pChan != NULL) {
	    if (stopFlag) {
		if (pChan->pFn == cauSigGenRamp)
		    pChan->pFn = NULL;
		else {
		    (void)printf("%s not in ramp mode\n", pCxCmd->pField);
		}
	    }
	    else if (pChan->pFn == cauSigGenRamp) {
		(void)printf("%s already in ramp mode\n", pCxCmd->pField);
	    }
	    else
		cauSigGenRampAdd(pCxCmd, pCauDesc, pChan);
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*+/subr**********************************************************************
* NAME	cauChanAdd - add a channel to a cau descriptor
*
* DESCRIPTION
*	This routine adds a channel to a cau descriptor.  When complete,
*	the channel descriptor will contain the graphics information for
*	the channel.
*
* RETURNS
*	pointer to CAU_CHAN structure, or
*	NULL
*
* BUGS
* o	doesn't check for duplication of channels already added
*
*-*/
static CAU_CHAN *
cauChanAdd(pCxCmd, pCauDesc, chanName)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
char	*chanName;	/* I channel name (.VAL assumed of field omitted) */
{
    long	stat;           /* status return from calls */
    CAU_CHAN	*pCauChan;	/* pointer to cau channel descriptor */
    char	message [80];
    chtype	getType;

    assert(pCauDesc != NULL);
    assert(chanName != NULL);
    assert(strlen(chanName) > 0);
    assert(strlen(chanName) < db_name_dim);

    if ((pCauChan = (CAU_CHAN *)malloc(sizeof(CAU_CHAN))) == NULL) {
	(void)printf("malloc error\n");
	return NULL;
    }
    pCauChan->pCxCmd = pCxCmd->pCxCmdRoot;
    pCauChan->pCh = NULL;
    pCauChan->pEv = NULL;
    pCauChan->pBuf = NULL;
    pCauChan->pGRBuf = NULL;
    pCauChan->pFn = NULL;
    pCauChan->interval = 0.;
    pCauChan->lastMonErr = 0;
    cauCaDebugName("prior to ca_search", chanName, 0);
    stat = ca_search(chanName, &pCauChan->pCh);
    cauCaDebugStat("back from ca_search", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error on search for %s\n", chanName);
	goto addError;
    }
    cauCaDebug("prior to ca_pend_io(1.0)", 0);
    stat = ca_pend_io(1.0);
    cauCaDebugStat("back from ca_pend_io(1.0)", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error on search for %s\n", chanName);
	goto addError;
    }
    strcpy(pCauChan->name, chanName);
    pCauChan->dbfType = ca_field_type(pCauChan->pCh);
    pCauChan->dbrType = dbf_type_to_DBR(ca_field_type(pCauChan->pCh));
    pCauChan->elCount = ca_element_count(pCauChan->pCh);
    if (pCauChan->elCount == 1)
	pCauChan->reqCount = 1;
    else if (pCauChan->elCount <= 512)
	pCauChan->reqCount = pCauChan->elCount;
    else
	pCauChan->reqCount = 512;
    pCauChan->units = NULL;

    pCauChan->pBuf = (union db_access_val *)malloc(
		sizeof(union db_access_val) + 
		dbr_value_size[pCauChan->dbfType] * pCauChan->elCount);
    if (pCauChan->pBuf == NULL) {
	(void)printf("malloc error\n");
	goto addError;
    }
    pCauChan->pBuf->tstrval.status = -2;
    pCauChan->pGRBuf =
		(union db_access_val *)malloc( sizeof(union db_access_val));
    if (pCauChan->pGRBuf == NULL) {
	(void)printf("malloc error\n");
	goto addError;
    }
    pCauChan->pGRBuf->gstrval.status = -2;
    getType = dbf_type_to_DBR_GR(pCauChan->dbfType);
    sprintf(message, "prior to ca_array_get (%s)", dbr_type_to_text(getType));
    cauCaDebug(message, 0);
    stat = ca_array_get(getType, 1, pCauChan->pCh, pCauChan->pGRBuf);
    cauCaDebugStat("back from ca_array_get", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error getting graphics info for %s\n", chanName);
	goto addError;
    }
    cauCaDebug("prior to ca_pend_io(1.0)", 0);
    stat = ca_pend_io(1.0);
    cauCaDebugStat("back from ca_pend_io", stat, 0);
    if (stat != ECA_NORMAL) {
	(void)printf("error getting graphics info for %s\n", chanName);
	goto addError;
    }
    if (pCauChan->dbfType == DBF_CHAR)
	pCauChan->units = pCauChan->pGRBuf->gchrval.units;
    else if (pCauChan->dbfType == DBF_SHORT)
	pCauChan->units = pCauChan->pGRBuf->gshrtval.units;
    else if (pCauChan->dbfType == DBF_LONG)
	pCauChan->units = pCauChan->pGRBuf->glngval.units;
    else if (pCauChan->dbfType == DBF_FLOAT)
	pCauChan->units = pCauChan->pGRBuf->gfltval.units;
    else if (pCauChan->dbfType == DBF_DOUBLE)
	pCauChan->units = pCauChan->pGRBuf->gdblval.units;

#ifdef vxWorks
    CauLock;
#endif
    DoubleListAppend(pCauChan, pCauDesc->pChanHead, pCauDesc->pChanTail);
#ifdef vxWorks
    CauUnlock;
#endif

    return pCauChan;
addError:
    if (pCauChan->pCh != NULL) {
	cauCaDebugName("prior to ca_clear_channel", pCauChan->name, 0);
	stat = ca_clear_channel(pCauChan->pCh);
	cauCaDebugStat("back from ca_clear_channel", stat, 0);
    }
    if (pCauChan->pBuf != NULL)
	free((char *)pCauChan->pBuf);
    if (pCauChan->pGRBuf != NULL)
	free((char *)pCauChan->pGRBuf);
    return NULL;
}

/*+/subr**********************************************************************
* NAME	cauChanDel - delete a channel from a cau descriptor
*
* DESCRIPTION
*	This routine deletes a channel from a cau descriptor.
*
* RETURNS
*	OK
*
*-*/
static long
cauChanDel(pCxCmd, pCauDesc, pCauChan)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
CAU_CHAN *pCauChan;	/* IO pointer to cau channel descriptor */
{
    long	stat;           /* status return from calls */

    assert(pCauDesc != NULL);
    assert(pCauChan != NULL);

#ifdef vxWorks
    CauLock;
#endif
    DoubleListRemove(pCauChan, pCauDesc->pChanHead, pCauDesc->pChanTail);
#ifdef vxWorks
    CauUnlock;
#endif

    if (pCauChan->pCh != NULL) {
	cauCaDebugName("prior to cau_clear_channel", pCauChan->name, 0);
	stat = ca_clear_channel(pCauChan->pCh);
	cauCaDebugStat("back from ca_clear_channel", stat, 0);
	if (stat != ECA_NORMAL) {
	    (void)printf("error closing pCh for: %s\n",
					    pCauChan->name);
	}
    }
    if (pCauChan->pBuf != NULL)
	free((char *)pCauChan->pBuf);
    free((char *)pCauChan);

    return OK;
}

/*+/subr**********************************************************************
* NAME	cauChanFind - find a channel in a cau descriptor
*
* DESCRIPTION
*	This routine finds a channel in a cau descriptor.
*
* RETURNS
*	CAU_CHAN * for channel, if found, or
*	NULL
*
*-*/
static CAU_CHAN *
cauChanFind(pCauDesc, chanName)
CAU_DESC *pCauDesc;	/* I pointer to cau descriptor */
char	*chanName;	/* I channel name to find in cau */
{
    CAU_CHAN	*pChan;		/* pointer to channel descriptor */

    assert(pCauDesc != NULL);
    assert(chanName != NULL);

    pChan = pCauDesc->pChanHead;
    while (pChan != NULL) {
        if (strcmp(pChan->name, chanName) == 0)
	    break;
	pChan = pChan->pNext;
    }

    return pChan;
}

/*+/subr**********************************************************************
* NAME	cauFree - free a cau descriptor, after cleaning up
*
* DESCRIPTION
*	This routine cleans up a cau descriptor, closing channels, etc.,
*	and then free()'s the data structures.
*
* RETURNS
*	OK
*
*-*/
static long
cauFree(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	retStat=OK;/* return status to caller */

    assert(pCauDesc != NULL);

    while (pCauDesc->pChanHead != NULL) {
	if (cauChanDel(pCxCmd, pCauDesc, pCauDesc->pChanHead) != OK)
	    (void)printf("cauFree: error deleting channel\n");
    }
    while (pCauDesc->pChanConnHead != NULL) {
	if (cauChanDel(pCxCmd, pCauDesc, pCauDesc->pChanConnHead) != OK)
	    (void)printf("cauFree: error deleting channel\n");
    }

    return retStat;
}

/*+/subr**********************************************************************
* NAME	cauGetAndPrint - get and print the value for a channel
*
* DESCRIPTION
*	This routine gets the current value for the channel and prints it
*
* RETURNS
*	void
*
*-*/
static void
cauGetAndPrint(pCxCmd,
		pCauDesc, pChan, printTime, printDBRType, printENUMAsShort)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
CAU_CHAN *pChan;	/* I channel pointer */
int	printTime;	/* I 1 if time is to be printed */
int	printDBRType;	/* I 1 if DBR_type of channel is to be printed */
int	printENUMAsShort;/* I 1 if DBR_ENUM is to be printed as short */
{
    long	stat;
    cauCaDebugDbrAndName("prior to ca_array_get",pChan->dbrType,pChan->name,0);
    stat = ca_array_get(pChan->dbrType, pChan->reqCount,pChan->pCh,pChan->pBuf);
    cauCaDebugStat("back from ca_array_get", stat, 0);
    if (stat != ECA_NORMAL)
	(void)printf("error on ca_array_get for %s \n", pChan->name);
    else {
	cauCaDebug("prior to ca_pend_io(1.0)", 0);
	stat = ca_pend_io(1.0);
	cauCaDebugStat("back from ca_pend_io(1.0)", stat, 0);
	if (stat != ECA_NORMAL)
	    (void)printf("error on ca_array_get for %s \n", pChan->name);
	else {
	    cauPrintBuf(pCxCmd,
		    pChan, 1, printTime, printDBRType, printENUMAsShort, 0);
	}
    }
}

/*+/subr**********************************************************************
* NAME	cauInitAtStartup - initialization for cau
*
* DESCRIPTION
*	Perform several initialization duties:
*	o   initialize an empty cau descriptor.  In order to
*	    use the CAU_DESC, channels must be specified.
*	o   initialize the command context block
*	o   initialize the help information
*
* RETURNS
*	void
*
*-*/
static void
cauInitAtStartup(pCauDesc, pCxCmd)
CAU_DESC *pCauDesc;	/* O pointer to cau descriptor */
CX_CMD	*pCxCmd;	/* I pointer to command context */
{
    pCauDesc->pCxCmd = pCxCmd;
    pCauDesc->pChanHead = NULL;
    pCauDesc->pChanTail = NULL;
    pCauDesc->pChanConnHead = NULL;
    pCauDesc->pChanConnTail = NULL;
    pCauDesc->secPerStep = .5;
    pCauDesc->nSteps = 10;
    pCauDesc->begVal = 0.;
    pCauDesc->endVal = 0.;

    cmdInitContext(pCxCmd, "  cau:  ");

/*-----------------------------------------------------------------------------
* help information initialization
*----------------------------------------------------------------------------*/
    helpInit(&pCxCmd->helpList);
/*-----------------------------------------------------------------------------
* help info--generic commands
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &pCxCmd->helpCmds, "commands", "\n\
Generic commands are:\n\
   dataOut    [filePath]     (default is to normal output)\n\
   bg\n\
   help       [topic]\n\
   quit (or ^D)\n\
   source     filePath\n\
");
/*-----------------------------------------------------------------------------
* help info--cau-specific commands
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &pCxCmd->helpCmdsSpec, "commands", "\n\
cau-specific commands are (the * isn't part of the command):\n\
   deadband      opt  (where opt is either MDEL or ADEL)\n\
   debug         [n]  (where n can be 0, 1, 2, or 3; if n omitted, level++)\n\
   debug-\n\
   delete        chanName [chanName ...]  (or  all )\n\
   get[,count]   [chanName [chanName ...]]\n\
  *info          [chanName [chanName ...]]\n\
  *interval,sec[,jitter]  [chanName [chanName ...]]\n\
   interval-     [chanName [chanName ...]]\n\
  *monitor[,count] [chanName [chanName ...]]\n\
   monitor-      [chanName [chanName ...]]\n\
   put           chanName value               (or \"value\")\n\
   ramp[,params] chanName [chanName ...]]  (use help ramp for more info)\n\
   ramp-         [chanName [chanName ...]]\n\
\n\
Output from commands flagged with * can be routed to a file by using the\n\
\"dataOut filePath\" command.  The present contents of the file are\n\
preserved, with new output being written at the end.\n\
");
/*-----------------------------------------------------------------------------
* help info--debug command
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &helpDebug, "debug", "\n\
   debug      [n]  (where n can be 0, 1, 2, or 3; if n omitted, level++)\n\
   debug-\n\
\n\
The debug command sets a `debug level' which is used to control the\n\
printing of debug information from Channel Access.  The debug command\n\
without a value increments the debug level; the debug- command decrements\n\
the debug level.  A specific level can be set by specifying a number\n\
following the debug command.  The levels and their result are:\n\
	0	don't print debug information\n\
	1	print message before and after most ca_xxx calls\n\
	2	print message at entry and exit to monitor handler\n\
	3	print message before and after ca_pend_event in main loop\n\
");
/*-----------------------------------------------------------------------------
* help info--bg command
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &pCxCmd->helpBg, "bg", "\n\
The bg command under vxWorks allows the cau process to continue\n\
running without accepting commands from the keyboard.  This allows the\n\
vxWorks shell to be used for other purposes without the need to\n\
terminate cau.\n\
\n\
Under SunOS, no bg command is directly available.  Instead, if cau\n\
is being run from the C shell (csh), it can be detached using several\n\
steps from the keyboard (with the first % on each line being the prompt\n\
from csh):\n\
	type ^Z, then type\n\
	% bg %cau\n\
\n\
To move cau to the foreground, type\n\
	% fg % cau\n\
");
/*-----------------------------------------------------------------------------
* help info--interval command
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &helpInterval, "interval", "\n\
The interval command provides for testing the interval between\n\
successive samples on a channel (or on several channels).  A Channel\n\
Access monitor is placed on the channel.  Each time a value is received,\n\
the difference between its time stamp and the time stamp of the prior\n\
value is computed.  If the difference is outside the specified tolerance,\n\
then a message is printed.  The two valid forms of the command are:\n\
\n\
  *interval,sec[,jitter]  [chanName [chanName ...]]\n\
   interval-  [chanName [chanName ...]]\n\
\n\
The allowable intervals are  \"sec - jitter\" to \"sec + jitter\", inclusive.\n\
Because of rounding errors, some leeway should be implicit in the\n\
specification of jitter.  For example, to test BSD_21N:00:00 for a 1\n\
second interval, plus or minus 4 milli-seconds, use:\n\
\n\
   interval,1,.0041 BSD_21N:00:00\n\
\n\
Several interval tests can be operating simultaneously on different sets\n\
of channels; a particular channel can't processed by two different interval\n\
commands at once.\n\
\n\
In addition to the functionality just described, each channel is checked\n\
periodically to see that it is still sending data.  If no value has been\n\
received for (1 second + intervalTime) then an error message is printed\n\
for the channel.\n\
");
/*-----------------------------------------------------------------------------
* help info--ramp command information
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &helpRamp, "ramp", "\n\
The ramp command can be used for numeric, enumerated, or string channels.\n\
Optional parameters specify the delay time between steps in a cycle,\n\
the number of steps per cycle, the beginning value, and the ending value.\n\
If a parameter isn't specified, its value isn't changed.  Commas must be\n\
used as `placeholders' if a parameter follows one or more omitted\n\
parameters.  The form (all parameters are optional) is:\n\
\n\
	ramp,secPerStep,nSteps,begVal,endVal [chanName[,chanName ...]]\n\
\n\
Default for secPerStep is .5; default for nSteps is 10.\n\
For numeric channels, the generated signal starts at the begVal, goes to\n\
the endVal, immediately changes to the begVal, and then repeats\n\
the process.  If begVal == endVal (the default at startup), then \n\
the signal starts at LOPR and ends at HOPR.\n\
\n\
For enumerated channels, the states are selected sequentially, beginning\n\
with zero-value, then the one-value, etc.\n\
\n\
For string channels (such as a state record), the generated signal is a\n\
varying length text string, which is composed of repetitions of the digits\n\
1 through 0.  begVal and endVal specify the beginning and ending length of\n\
the string, in characters; default is 0 and 10, respectively.\n\
");
/*-----------------------------------------------------------------------------
* help info--cau usage information
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &pCxCmd->helpUsage, "usage", "\n\
cau is a Channel Access Utility--it provides an easy interface to Channel\n\
Access services.  cau also provides services in addition to those available\n\
through Channel Access.\n\
\n\
Most cau commands allow specifying one or more channel names.  Any names\n\
specified on a command are added to the list of names which have already\n\
been used.  If names are specified for a command, then the command applies\n\
only to those channels; if no names are specified, then the command applies\n\
to all channels in the list.\n\
\n\
Some commands produce output which can be routed to a file with the\n\
\"dataOut filePath\" command.  Use \"help commands\" for more information.\n\
\n\
Some commands can be immediately followed by a - (with no intervening\n\
blanks).  When used in this way, the action of the command is reversed,\n\
either for the specified channels or for the entire list of channels.\n\
\n\
For information on moving cau into the background, use the \"help bg\"\n\
command.\n\
");
}

/*+/subr**********************************************************************
* NAME	cauMonitor - receive monitor buffer from Channel Access
*
* DESCRIPTION
*	Copies the value into the channel's buffer and prints the value.
*
*	For channels with interval checking enabled (as indicated by a
*	non-zero .interval item), the interval checking is done.  The
*	value is printed only for the initial value and for violations
*	of the interval criteria.
*
* RETURNS
*	void
*
* BUGS
* o	assumes that type and count match the buffer
* o	only prints first element for array channels
*
* NOTES
* 1.	If debug level is 2 or more, then a time stamped message is
*	printed when this routine is entered, and another is printed
*	when it exits.
*
*-*/
static void
cauMonitor(arg)
struct event_handler_args arg;
{
    CAU_CHAN	*pCauChan;	/* pointer to channel descriptor */
    int		nBytes;		/* total size of `value' buffer */
    double	interval;	/* difference between present and prev stamp */
    char 	priorStampText[28];/* time stamp of prior value */
    double	diff;		/* diff between actual and desired intervals */
    int		printFlag=1;
    CX_CMD	*pCxCmd;	/* pointer to command context */
    char	message[80];
    char	nowText[28];
    char	chanTsText[28];

    pCauChan = (CAU_CHAN *)arg.usr;
    pCxCmd = pCauChan->pCxCmd;

    (void)epicsTimeGetCurrent(&pCauChan->lastMonTime);
    if (pCauChan->lastMonErr != 0) {
        (void)epicsTimeToStrftime(nowText,28,"%m-%d-%y %H:%M:%S.%09f",&pCauChan->lastMonTime);
        (void)epicsTimeToStrftime(chanTsText,28,"%m-%d-%y %H:%M:%S.%09f",
                                                &((struct dbr_time_string *)arg.dbr)->stamp);
	(void)fprintf(pCxCmd->dataOut,
	    "resume for %s at %s (local) or %s (ioc)\n", pCauChan->name, nowText,chanTsText);
	if (pCxCmd->dataOut != stdout) {
	    epicsTimeToStrftime(nowText,28,"%m-%d-%y %H:%M:%S.%09f",&pCauChan->lastMonTime);
	    epicsTimeToStrftime(chanTsText,28,"%m-%d-%y %H:%M:%S.%09f",
                                                &((struct dbr_time_string *)arg.dbr)->stamp);
	    (void)fprintf(pCxCmd->dataOut,
		"resume for %s at %s (local) or %s (ioc)\n", pCauChan->name, nowText,chanTsText);
	}
	pCauChan->lastMonErr = 0;
    }
    if (glCauDebug > 1) {
	nBytes = dbr_size_n(arg.type, arg.count);
	(void)sprintf(message, "enter cauMonitor() for %s %s %d",
		pCauChan->name, dbr_type_to_text(arg.type), nBytes);
	cauCaDebug(message, 1);
    }
    if (pCauChan->interval > 0. && pCauChan->pBuf->tstrval.status != -2) {

	interval = epicsTimeDiffInSeconds (&((struct dbr_time_string *)arg.dbr)->stamp,
                                                &pCauChan->pBuf->tstrval.stamp);
	diff = pCauChan->interval - interval;
	if (diff < 0.)
	    diff = -diff;
	if (diff > pCauChan->jitter) {
	    (void)epicsTimeToStrftime(priorStampText,28,"%m-%d-%y %H:%M:%S.%09f",
                                &pCauChan->pBuf->tstrval.stamp);
	    (void)fprintf(pCxCmd->dataOut,
		    "interval from prior (at %s) to following is %.3f\n",
                    priorStampText, interval);
	}
	else
	    printFlag = 0;
    }
    nBytes = dbr_size_n(arg.type, arg.count);
    while (nBytes-- > 0)
	((char *)pCauChan->pBuf)[nBytes] = ((char *)arg.dbr)[nBytes];
    if (printFlag)
	cauPrintBuf(pCxCmd, pCauChan, 1, 1, 0, 0, 0);
    cauCaDebug("exit cauMonitor()", 1);
}

/*+/subr**********************************************************************
* NAME	cauPrintBuf - print a channel's present value
*
* DESCRIPTION
*	Print buffer type, channel name, time stamp, and value.
*
* RETURNS
*	void
*
*-*/
static void
cauPrintBuf(pCxCmd, pChan, prName, prTime, prDBRType, prENUMAsShort, prEGU)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_CHAN *pChan;	/* I pointer to channel descriptor */
int	prName;		/* I 1 if channel name is to be printed */
int	prTime;		/* I 1 if time is to be printed */
int	prDBRType;	/* I 1 if DBR_type of channel is to be printed */
int	prENUMAsShort;	/* I 1 if DBR_ENUM values are to print as short */
int	prEGU;		/* I 1 if EGU is to be printed */
{
    char	stampText[28];
    int		prec;
    int		state;		/* state for ENUM's */
    void	*pVal;		/* pointer to value field */

    if (prDBRType)
      (void)fprintf(pCxCmd->dataOut,"%-10s ",dbr_type_to_text(pChan->dbrType));
    if (prName)
	(void)fprintf(pCxCmd->dataOut, "%20s", pChan->name);
    if (!prTime)
	;
    else if (dbr_type_is_TIME(pChan->dbrType)) {
        (void)epicsTimeToStrftime(stampText,28,"%m-%d-%y %H:%M:%S.%09f"
                                                ,&pChan->pBuf->tstrval.stamp);
	(void)fprintf(pCxCmd->dataOut, " %s", &stampText[9]);
    }
    else {
	(void)fprintf(pCxCmd->dataOut, " buffer not of type DBR_TIME_xxx\n");
	return;
    }

    if (pChan->reqCount > 1) {
	cauPrintBufArray(pCxCmd->dataOut, pChan);
	return;
    }
    pVal = dbr_value_ptr(pChan->pBuf, pChan->dbrType);
    if (pVal == NULL)
	(void)fprintf(pCxCmd->dataOut,"invalid buffer type: %ld", pChan->dbrType);
    else if (dbr_type_is_STRING(pChan->dbrType))
	(void)fprintf(pCxCmd->dataOut, " %12s", (char *)pVal);
    else if (dbr_type_is_SHORT(pChan->dbrType))
	(void)fprintf(pCxCmd->dataOut, " %12d", *(short *)pVal);
    else if (dbr_type_is_LONG(pChan->dbrType))
	(void)fprintf(pCxCmd->dataOut, " %12d", *(int *)pVal);
    else if (dbr_type_is_CHAR(pChan->dbrType))
	(void)fprintf(pCxCmd->dataOut, " %12d", *(unsigned char *)pVal);
    else if (dbr_type_is_ENUM(pChan->dbrType)) {
	state = *(short *)pVal;
	if (pChan->dbfType != DBF_ENUM || prENUMAsShort)
	    (void)fprintf(pCxCmd->dataOut, " %12d", state);
	else if (state < 0 || state >= pChan->pGRBuf->genmval.no_str)
	    (void)fprintf(pCxCmd->dataOut, " %12d (illegal)", state);
	else {
	    (void)fprintf(pCxCmd->dataOut,
				" %12s", pChan->pGRBuf->genmval.strs[state]);
	}
    }
    else if (dbr_type_is_FLOAT(pChan->dbrType)) {
	prec = pChan->pGRBuf->gfltval.precision;
	(void)fprintf(pCxCmd->dataOut, " %12.*f", prec, *(float *)pVal);
    }
    else if (dbr_type_is_DOUBLE(pChan->dbrType)) {
	prec = pChan->pGRBuf->gdblval.precision;
	(void)fprintf(pCxCmd->dataOut, " %12.*f", prec, *(double *)pVal);
    }
    if (pChan->units != NULL && prEGU)
	(void)fprintf(pCxCmd->dataOut, " %s", pChan->units);
    (void)fprintf(pCxCmd->dataOut, "\n");
}

/*+/subr**********************************************************************
* NAME	cauPrintBufArray - print an array channel's present value
*
* DESCRIPTION
*	Print values for an array channel, in compressed form
*
* RETURNS
*	void
*
*-*/
static  void cauPrintBufArray(out, pChan)
FILE	*out;
CAU_CHAN *pChan;
{
    int		nEl, nBytes, i, prec;
    char	*pSrc;
    char	text[7];
    chtype	dbrType=pChan->dbrType;

    (void)fprintf(out, "\n");
    nEl = pChan->reqCount;
    nBytes = dbr_value_size[dbrType];
    pSrc = (char *)dbr_value_ptr(pChan->pBuf, dbrType);

    if      (dbr_type_is_FLOAT(dbrType)) prec=pChan->pGRBuf->gfltval.precision;
    else if (dbr_type_is_SHORT(dbrType)) prec = 0;
    else if (dbr_type_is_DOUBLE(dbrType))prec=pChan->pGRBuf->gdblval.precision;
    else if (dbr_type_is_LONG(dbrType))  prec = 0;
    else if (dbr_type_is_CHAR(dbrType))  prec = 0;
    else if (dbr_type_is_ENUM(dbrType))  prec = 0;
    else 				 prec = 0;

    for (i=0; i<nEl; i++) {
	if (i % 10 == 0)
	    (void)fprintf(out, "%05d", i);
        if      (dbr_type_is_FLOAT(dbrType))
	    cvtDblToTxt(text, 6, (double)*(float *)pSrc, prec);
        else if (dbr_type_is_SHORT(dbrType))
	    cvtLngToTxt(text, 6, (long)*(short *)pSrc);
        else if (dbr_type_is_DOUBLE(dbrType))
	    cvtDblToTxt(text, 6, *(double *)pSrc, prec);
        else if (dbr_type_is_LONG(dbrType))
	    cvtLngToTxt(text, 6, *(long *)pSrc);
        else if (dbr_type_is_CHAR(dbrType))
	    cvtLngToTxt(text, 6, (long)*(unsigned char *)pSrc);
        else if (dbr_type_is_ENUM(dbrType))
	    cvtLngToTxt(text, 6, (long)*(short *)pSrc);
	(void)fprintf(out, " %6s", text);
	if ((i+1) % 10 == 0 || i+1 >= nEl)
	    (void)fprintf(out, "\n");
	pSrc += nBytes;
    }
}

/*+/subr**********************************************************************
* NAME	cauPrintInfo - print some information about a channel
*
* DESCRIPTION
*	Prints channel name, native type and count, and indicates whether
*	the two buffers (DBR_GR_xxx and DBR_TIME_xxx) have received
*	values from the IOC.
*
* RETURNS
*	void
*
*-*/
static void
cauPrintInfo(pCxCmd, pChan)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_CHAN *pChan;	/* I pointer to channel descriptor */
{
    (void)fprintf(pCxCmd->dataOut, "%20s", pChan->name);
    if (dbf_type_is_valid(pChan->dbfType))
	(void)fprintf(pCxCmd->dataOut,
			" %16s", dbf_type_to_text(pChan->dbfType));
    else
	(void)fprintf(pCxCmd->dataOut, "dbfType=%8ld", pChan->dbfType);
    (void)fprintf(pCxCmd->dataOut, " elCount=%5d", pChan->elCount);

    if (pChan->pBuf->tstrval.status == -2)
	(void)fprintf(pCxCmd->dataOut, "\nno value has been received");
    else
	cauPrintBuf(pCxCmd, pChan, 0, 0, 0, 0, 1);

    if (pChan->pGRBuf->gstrval.status == -2)
	(void)fprintf(pCxCmd->dataOut,
			"\nno DBR_GR_... information has been received");

    (void)fprintf(pCxCmd->dataOut, "\n");
}

/*+/subr**********************************************************************
* NAME	cauSigGen - make a signal generation pass, doing ca_put's
*
* DESCRIPTION
*	For all channels in the list which specify a signal generation
*	function:
*	o  decrement the counter
*	o  if the counter has reached zero,
*	   -  call the function and
*	   -  do a ca_put for the new value; then
*	   -  restart the counter with the base count
*
*	If any ca_put calls were actually made, ca_flush_io is called.
*
* RETURNS
*	void
*
*-*/
static void
cauSigGen(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_DESC	*pCauDesc;	/* IO pointer to cau descriptor */
{
    CAU_CHAN	*pChan;		/* pointer to channel descriptor */
    long	stat;           /* status return from calls */
    int		count=0;
    TS_STAMP	now;		/* present time */

    assert(pCauDesc != NULL);

    (void)epicsTimeGetCurrent(&now);
    pChan = pCauDesc->pChanHead;
    while (pChan != NULL) {
	if (pChan->pFn != NULL) {
	    if ( epicsTimeGreaterThanEqual(&now, &pChan->nextTime)) {  /*true if left >= right */

		(pChan->pFn)(pCxCmd, pChan);
		count += cauSigGenPut(pCxCmd, pChan);
		epicsTimeAddSeconds(&pChan->nextTime, pChan->secPerStep);
	    }
	}
	pChan = pChan->pNext;
    }
    if (count) {
	cauCaDebug("prior to ca_flush_io", 0);
	stat = ca_flush_io();
	cauCaDebugStat("back from ca_flush_io", stat, 0);
    }
}

/*+/subr**********************************************************************
* NAME	cauSigGenGetParams - get signal generation parameters
*
* DESCRIPTION
*	Scans the optional signal generation parameters following a
*	signal generation command.  If any legal parameters are specified,
*	they are used to change the default parameters.
*
* RETURNS
*	OK, or
*	ERROR
*
*-*/
static long
cauSigGenGetParams(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    double	secPerStep;
    double	begVal;
    double	endVal;
    int		nSteps;
    int		fldLen;

/* secPerStep */
    if (pCxCmd->delim == ',') {
	fldLen = nextFltFieldAsDbl(&pCxCmd->pLine, &secPerStep, &pCxCmd->delim);
	if (fldLen <= 1 && pCxCmd->delim != ',') {
	    (void)printf("error on seconds per step field\n");
	    return ERROR;
	}
	else if (fldLen <= 1)
	    secPerStep = pCauDesc->secPerStep;
	else if(secPerStep < .1) {
	    (void)printf("error on seconds per step field\n");
	    return ERROR;
	}
    }
    else
	secPerStep = pCauDesc->secPerStep;

/* nSteps */
    if (pCxCmd->delim == ',') {
	fldLen = nextIntFieldAsInt(&pCxCmd->pLine, &nSteps, &pCxCmd->delim);
	if (fldLen <= 1 && pCxCmd->delim != ',') {
	    (void)printf("error on steps per cycle\n");
	    return ERROR;
	}
	else if (fldLen <= 1)
	    nSteps = pCauDesc->nSteps;
	else if (nSteps < 1) {
	    (void)printf("error on steps per cycle\n");
	    return ERROR;
	}
    }
    else
	nSteps = pCauDesc->nSteps;

/* begVal */
    if (pCxCmd->delim == ',') {
	fldLen = nextFltFieldAsDbl(&pCxCmd->pLine, &begVal, &pCxCmd->delim);
	if (fldLen <= 1 && pCxCmd->delim != ',') {
	    (void)printf("error on begin value\n");
	    return ERROR;
	}
	else if (fldLen <= 1)
	    begVal = pCauDesc->begVal;
    }
    else
	begVal = pCauDesc->begVal;

/* endVal */
    if (pCxCmd->delim == ',') {
	fldLen = nextFltFieldAsDbl(&pCxCmd->pLine, &endVal, &pCxCmd->delim);
	if (fldLen <= 1 && pCxCmd->delim != ',') {
	    (void)printf("error on end value\n");
	    return ERROR;
	}
	else if (fldLen <= 1)
	    endVal = pCauDesc->endVal;
    }
    else
	endVal = pCauDesc->endVal;

    pCauDesc->secPerStep = secPerStep;
    pCauDesc->nSteps = nSteps;
    pCauDesc->begVal = begVal;
    pCauDesc->endVal = endVal;

    return OK;
}

/*+/subr**********************************************************************
* NAME	cauSigGenPut - store a new value for a channel
*
* DESCRIPTION
*	Sends, with ca_put, the .currVal item for the channel, using
*	the native type.
*
* RETURNS
*	number of ca_put's done
*
* BUGS
* o	handles only scalars
*
*-*/
static long
cauSigGenPut(pCxCmd, pCauChan)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_CHAN *pCauChan;	/* channel pointer */
{
    long	stat;           /* status return from calls */
    int		count=0;

    if (pCauChan->dbfType == DBF_STRING) {
	cauCaDebug("prior to ca_put--DBR_STRING", 0);
	stat = ca_put(DBR_STRING, pCauChan->pCh, (void *)pCauChan->str.string);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_FLOAT) {
	cauCaDebug("prior to ca_put--DBR_FLOAT", 0);
	stat = ca_put(DBR_FLOAT, pCauChan->pCh, (void *)&pCauChan->flt.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_SHORT) {
	cauCaDebug("prior to ca_put--DBR_SHORT", 0);
	stat = ca_put(DBR_SHORT, pCauChan->pCh,(void *)&pCauChan->shrt.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_ENUM) {
	cauCaDebug("prior to ca_put--DBR_ENUM", 0);
	stat = ca_put(DBR_ENUM, pCauChan->pCh, (void *)&pCauChan->enm.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_DOUBLE) {
	cauCaDebug("prior to ca_put--DBR_DOUBLE", 0);
	stat = ca_put(DBR_DOUBLE,pCauChan->pCh,(void *)&pCauChan->dbl.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_LONG) {
	cauCaDebug("prior to ca_put--DBR_LONG", 0);
	stat = ca_put(DBR_LONG, pCauChan->pCh,(void *)&pCauChan->lng.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }
    else if (pCauChan->dbfType == DBF_CHAR) {
	cauCaDebug("prior to ca_put--DBR_CHAR", 0);
	stat = ca_put(DBR_CHAR, pCauChan->pCh,(void *)&pCauChan->chr.currVal);
	cauCaDebugStat("back from ca_put", stat, 0);
	count++;
    }

    return count;
}

static char *cauRampString="1234567890123456789012345678901234567890";
/*+/subr**********************************************************************
* NAME	cauSigGenRamp - generate ramp function
*
* DESCRIPTION
*
* RETURNS
*	number of ca_put's done
*
* BUGS
* o	text
*
*-*/
static long
cauSigGenRamp(pCxCmd, pCauChan)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_CHAN *pCauChan;	/* channel pointer */
{
    int		count=0;

    if (pCauChan->dbfType == DBF_STRING) {
	pCauChan->str.currVal += pCauChan->str.addVal;
	strcpy(pCauChan->str.string, cauRampString);
	if (pCauChan->str.currVal < db_strval_dim)
	    pCauChan->str.string[pCauChan->str.currVal] = '\0';
	else
	    pCauChan->str.string[db_strval_dim-1] = '\0';
	if (pCauChan->str.endVal > pCauChan->str.begVal) {
	    if (pCauChan->str.currVal >= pCauChan->str.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->str.currVal = pCauChan->str.begVal;
		strcpy(pCauChan->str.string, cauRampString);
		if (pCauChan->str.currVal < db_strval_dim)
		    pCauChan->str.string[pCauChan->str.currVal] = '\0';
		else
		    pCauChan->str.string[db_strval_dim-1] = '\0';
	    }
	}
	else {
	    if (pCauChan->str.currVal <= pCauChan->str.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->str.currVal = pCauChan->str.begVal;
		strcpy(pCauChan->str.string, cauRampString);
		if (pCauChan->str.currVal < db_strval_dim)
		    pCauChan->str.string[pCauChan->str.currVal] = '\0';
		else
		    pCauChan->str.string[db_strval_dim-1] = '\0';
	    }
	}
    }
    else if (pCauChan->dbfType == DBF_FLOAT) {
	pCauChan->flt.currVal += pCauChan->flt.addVal;
	if (pCauChan->flt.endVal > pCauChan->flt.begVal) {
	    if (pCauChan->flt.currVal >= pCauChan->flt.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->flt.currVal = pCauChan->flt.begVal;
	    }
	}
	else {
	    if (pCauChan->flt.currVal <= pCauChan->flt.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->flt.currVal = pCauChan->flt.begVal;
	    }
	}
    }
    else if (pCauChan->dbfType == DBF_SHORT) {
	pCauChan->shrt.currVal += pCauChan->shrt.addVal;
	if (pCauChan->shrt.endVal > pCauChan->shrt.begVal) {
	    if (pCauChan->shrt.currVal >= pCauChan->shrt.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->shrt.currVal = pCauChan->shrt.begVal;
	    }
	}
	else {
	    if (pCauChan->shrt.currVal <= pCauChan->shrt.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->shrt.currVal = pCauChan->shrt.begVal;
	    }
	}
    }
    else if (pCauChan->dbfType == DBF_ENUM) {
	pCauChan->enm.currVal += pCauChan->enm.addVal;
	if (pCauChan->enm.currVal > pCauChan->enm.endVal)
	    pCauChan->enm.currVal = pCauChan->enm.begVal;
    }
    else if (pCauChan->dbfType == DBF_DOUBLE) {
	pCauChan->dbl.currVal += pCauChan->dbl.addVal;
	if (pCauChan->dbl.endVal > pCauChan->dbl.begVal) {
	    if (pCauChan->dbl.currVal >= pCauChan->dbl.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->dbl.currVal = pCauChan->dbl.begVal;
	    }
	}
	else {
	    if (pCauChan->dbl.currVal <= pCauChan->dbl.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->dbl.currVal = pCauChan->dbl.begVal;
	    }
	}
    }
    else if (pCauChan->dbfType == DBF_LONG) {
	pCauChan->lng.currVal += pCauChan->lng.addVal;
	if (pCauChan->lng.endVal > pCauChan->lng.begVal) {
	    if (pCauChan->lng.currVal >= pCauChan->lng.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->lng.currVal = pCauChan->lng.begVal;
	    }
	}
	else {
	    if (pCauChan->lng.currVal <= pCauChan->lng.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->lng.currVal = pCauChan->lng.begVal;
	    }
	}
    }
    else if (pCauChan->dbfType == DBF_CHAR) {
	pCauChan->chr.currVal += pCauChan->chr.addVal;
	if (pCauChan->chr.endVal > pCauChan->chr.begVal) {
	    if (pCauChan->chr.currVal >= pCauChan->chr.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->chr.currVal = pCauChan->chr.begVal;
	    }
	}
	else {
	    if (pCauChan->chr.currVal <= pCauChan->chr.endVal) {
		count += cauSigGenPut(pCxCmd, pCauChan);
		pCauChan->chr.currVal = pCauChan->chr.begVal;
	    }
	}
    }

    return count;
}
static void
cauSigGenRampAdd(pCxCmd, pCauDesc, pChan)
CX_CMD	*pCxCmd;	/* I pointer to command context */
CAU_DESC *pCauDesc;	/* I pointer to cau descriptor */
CAU_CHAN *pChan;	/* IO channel pointer */
{
    short	shrtDiff;	/* endVal-begVal for short */
    long	lngDiff;	/* endVal-begVal for long */
    char	chrDiff;	/* endVal-begVal for char */
    TS_STAMP	now;		/* present time */

    (void)epicsTimeGetCurrent(&now);
    pChan->secPerStep = pCauDesc->secPerStep;
    pChan->nextTime = now;

    if (pChan->dbfType == DBF_STRING) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->str.endVal = 10;
	    pChan->str.begVal = 0;
	}
	else {
	    pChan->str.endVal = (char)pCauDesc->endVal;
	    if (pChan->str.endVal < 0)
		pChan->str.endVal = 0;
	    else if (pChan->str.endVal >= db_strval_dim)
		pChan->str.endVal = db_strval_dim-1;
	    pChan->str.begVal = (char)pCauDesc->begVal;
	    if (pChan->str.begVal < 0)
		pChan->str.begVal = 0;
	    else if (pChan->str.begVal >= db_strval_dim)
		pChan->str.begVal = db_strval_dim-1;
	    if (pChan->str.endVal == pChan->str.begVal) {
		pChan->str.endVal = 10;
		pChan->str.begVal = 0;
	    }
	}
	shrtDiff = pChan->str.endVal - pChan->str.begVal;
	if (shrtDiff < 0)
	    shrtDiff = -shrtDiff;
	if (shrtDiff < pChan->nSteps)
	    pChan->nSteps = shrtDiff;
	pChan->str.addVal = (pChan->str.endVal - pChan->str.begVal) /
								pChan->nSteps;
	pChan->str.currVal = pChan->str.begVal;
	strcpy(pChan->str.string, cauRampString);
	pChan->str.string[pChan->str.currVal] = '\0';
    }
    else if (pChan->dbfType == DBF_FLOAT) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->flt.endVal = pChan->pGRBuf->gfltval.upper_disp_limit;
	    pChan->flt.begVal = pChan->pGRBuf->gfltval.lower_disp_limit;
	}
	else {
	    pChan->flt.endVal = pCauDesc->endVal;
	    pChan->flt.begVal = pCauDesc->begVal;
	}
	pChan->flt.addVal = (pChan->flt.endVal - pChan->flt.begVal) /
				((float)pChan->nSteps - .00001);
	pChan->flt.currVal = pChan->flt.begVal;
    }
    else if (pChan->dbfType == DBF_SHORT) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->shrt.endVal = pChan->pGRBuf->gshrtval.upper_disp_limit;
	    pChan->shrt.begVal = pChan->pGRBuf->gshrtval.lower_disp_limit;
	}
	else {
	    pChan->shrt.endVal = (short)pCauDesc->endVal;
	    pChan->shrt.begVal = (short)pCauDesc->begVal;
	}
	shrtDiff = pChan->shrt.endVal - pChan->shrt.begVal;
	if (shrtDiff < 0)
	    shrtDiff = -shrtDiff;
	if (shrtDiff < pChan->nSteps)
	    pChan->nSteps = shrtDiff;
	pChan->shrt.addVal = (pChan->shrt.endVal - pChan->shrt.begVal) /
								pChan->nSteps;
	pChan->shrt.currVal = pChan->shrt.begVal;
    }
    else if (pChan->dbfType == DBF_ENUM) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pChan->pGRBuf->genmval.no_str;
	pChan->enm.endVal = pChan->pGRBuf->genmval.no_str - 1;
	pChan->enm.begVal = 0;
	pChan->enm.addVal = 1;
	pChan->enm.currVal = pChan->enm.endVal;
    }
    else if (pChan->dbfType == DBF_DOUBLE) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->dbl.endVal = pChan->pGRBuf->gdblval.upper_disp_limit;
	    pChan->dbl.begVal = pChan->pGRBuf->gdblval.lower_disp_limit;
	}
	else {
	    pChan->dbl.endVal = pCauDesc->endVal;
	    pChan->dbl.begVal = pCauDesc->begVal;
	}
	pChan->dbl.addVal = (pChan->dbl.endVal - pChan->dbl.begVal) /
				((double)pChan->nSteps - .00001);
	pChan->dbl.currVal = pChan->dbl.begVal;
    }
    else if (pChan->dbfType == DBF_LONG) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->lng.endVal = pChan->pGRBuf->glngval.upper_disp_limit;
	    pChan->lng.begVal = pChan->pGRBuf->glngval.lower_disp_limit;
	}
	else {
	    pChan->lng.endVal = (long)pCauDesc->endVal;
	    pChan->lng.begVal = (long)pCauDesc->begVal;
	}
	lngDiff = pChan->lng.endVal - pChan->lng.begVal;
	if (lngDiff < 0)
	    lngDiff = -lngDiff;
	if (lngDiff < pChan->nSteps)
	    pChan->nSteps = lngDiff;
	pChan->lng.addVal = (pChan->lng.endVal - pChan->lng.begVal) /
								pChan->nSteps;
	pChan->lng.currVal = pChan->lng.begVal;
    }
    else if (pChan->dbfType == DBF_CHAR) {
	pChan->pFn = cauSigGenRamp;
	pChan->nSteps = pCauDesc->nSteps;
	if (pCauDesc->endVal == pCauDesc->begVal) {
	    pChan->chr.endVal = pChan->pGRBuf->gchrval.upper_disp_limit;
	    pChan->chr.begVal = pChan->pGRBuf->gchrval.lower_disp_limit;
	}
	else {
	    pChan->chr.endVal = (char)pCauDesc->endVal;
	    pChan->chr.begVal = (char)pCauDesc->begVal;
	}
	chrDiff = pChan->chr.endVal - pChan->chr.begVal;
	if (chrDiff < 0)
	    chrDiff = -chrDiff;
	if (chrDiff < pChan->nSteps)
	    pChan->nSteps = chrDiff;
	pChan->chr.addVal = (pChan->chr.endVal - pChan->chr.begVal) /
								pChan->nSteps;
	pChan->chr.currVal = pChan->chr.begVal;
    }
    else {
	printf("%s doesn't have ramp implemented yet\n", pChan->name);
    }
}
