/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution. 
\*************************************************************************/


/* this buffer type array must be kept in sync with the buffer pointer
arrays used by the test command */
static chtype bufTypes[]={DBR_STRING, DBR_SHORT, DBR_FLOAT, DBR_ENUM,
			DBR_CHAR, DBR_LONG, DBR_DOUBLE, -1};
/*/subhead testGet-----------------------------------------------------------
* testGet [chanName [chanName ...]]
*----------------------------------------------------------------------------*/
static
cau_testGet(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	stat;
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    int		i;

    pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    if (pCxCmd->fldLen <= 1 || strcmp(pCxCmd->pField, "all") == 0) {
	if ((pChan = pCauDesc->pChanHead) == NULL) {
	    (void)printf("no channels selected\n");
	    return;
	}
	while (pChan != NULL) {
	    for (i=0; bufTypes[i]>=0; i++) {
		pChan->dbrType = bufTypes[i];
		cauGetAndPrint(pCxCmd, pCauDesc, pChan, 0, 1, 1);
	    }
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
	    for (i=0; bufTypes[i]>=0; i++) {
		pChan->dbrType = bufTypes[i];
		cauGetAndPrint(pCxCmd, pCauDesc, pChan, 0, 1, 1);
	    }
	}
	pCxCmd->fldLen =
	    nextChanNameField(&pCxCmd->pLine, &pCxCmd->pField, &pCxCmd->delim);
    }
}

/*/subhead test-----------------------------------------------------------
* test[,quiet] chanName value [intChk fltChk fltDev] (or \"value\")
*----------------------------------------------------------------------------*/
static char valStr[db_strval_dim], *pExpStr;
static short valShrt;
static float valFlt;
static short valEnm;
static unsigned char valChr;
static long valLng, expLng;
static double valDbl, expDbl, devDbl;
/* these buffer pointer arrays must be kept in sync with the buffer type
array (just prior to the testGet command) */
static void *ppVal[]={(void *)valStr,(void *)&valShrt,(void *)&valFlt,
	(void *)&valEnm,(void *)&valChr,(void *)&valLng,(void *)&valDbl};
static void *ppExp[]={(void *)valStr,(void *)&valShrt,(void *)&valFlt,
	(void *)&valEnm,(void *)&valChr,(void *)&valLng,(void *)&valDbl};
static void *ppDev[]={(void *)valStr,(void *)&valShrt,(void *)&valFlt,
	(void *)&valEnm,(void *)&valChr,(void *)&valLng,(void *)&valDbl};
#define TEST_PRINT (void)fprintf(pCxCmd->dataOut,
static
cau_test(pCxCmd, pCauDesc)
CX_CMD	*pCxCmd;	/* IO pointer to command context */
CAU_DESC *pCauDesc;	/* IO pointer to cau descriptor */
{
    long	stat;
    CAU_CHAN	*pChan;		/* temp for channel pointer */
    char	*name;
    int		i, fldLen, quiet=0;
    chtype	putType, getType;
    char	delim, *pField, message[80];
    long	check=0;

    if (pCxCmd->delim == ',') {
	fldLen = nextAlphField(&pCxCmd->pLine, &pField, &delim);
	if (fldLen <= 1 || strcmp(pField, "quiet") != 0) {
	    (void)printf("error on `,quiet' field\n");
	    return;
	}
	quiet = 1;
    }
    if ((fldLen = nextChanNameField(&pCxCmd->pLine, &name, &delim)) <= 1)
	goto testError;
    if ((fldLen = nextNonSpaceField(&pCxCmd->pLine, &pExpStr, &delim)) <= 1)
	goto testError;
    if ((pChan = cauChanFind(pCauDesc, name)) == NULL) {
	if ((pChan = cauChanAdd(pCxCmd, pCauDesc, name)) == NULL) {
	    (void)printf("couldn't open %s \n", name);
	    return;
	}
    }
    if (pChan->elCount != 1) {
	(void)printf("can't use test for arrays or waveforms\n");
	return;
    }
/*-----------------------------------------------------------------------------
*    find out if checking is to be done, and set up the pointer arrays if so
*----------------------------------------------------------------------------*/
    if ((fldLen = nextIntFieldAsLong(&pCxCmd->pLine, &expLng, &delim)) > 1) {
	check = 1;
	if ((fldLen=nextFltFieldAsDbl(&pCxCmd->pLine, &expDbl, &delim)) <= 1) {
	    (void)printf("error on fltChk field\n");
	    return;
	}
	if ((fldLen=nextFltFieldAsDbl(&pCxCmd->pLine, &devDbl, &delim)) <= 1) {
	    (void)printf("error on fltDev field\n");
	    return;
	}
	for (i=0; bufTypes[i]>=0; i++) {
	    if (bufTypes[i] == DBR_STRING) {
		ppExp[i] = (void *)pExpStr;
		ppDev[i] = (void *)NULL;
	    }
	    else if (bufTypes[i] == DBR_SHORT ||
			bufTypes[i] == DBR_LONG ||
			bufTypes[i] == DBR_ENUM ||
			bufTypes[i] == DBR_CHAR) {
		ppExp[i] = (void *)&expLng;
		ppDev[i] = (void *)NULL;
	    }
	    else {
		ppExp[i] = (void *)&expDbl;
		ppDev[i] = (void *)&devDbl;
	    }
	}
    }
/*-----------------------------------------------------------------------------
*    put the value (DBR_STRING), get the value for each DBR_xxx, and
*    optionally compare the resultant values to the check values
*----------------------------------------------------------------------------*/
    assert(bufTypes[0] == DBR_STRING);
    CauCaDebug("prior to ca_put (DBR_STRING)", 0);
    stat = ca_put(DBR_STRING, pChan->pCh, pExpStr);
    CauCaDebugStat("back from ca_put", 0);
    if (stat != ECA_NORMAL) {
	TEST_PRINT "error on ca_put\n");
	return;
    }
    CauCaDebug("prior to ca_pend_io(1.0)", 0);
    stat = ca_pend_io(1.0);
    CauCaDebugStat("back from ca_pend_io", 0);
    if (stat != ECA_NORMAL) {
	TEST_PRINT "error on ca_put\n");
	return;
    }
    if (!quiet)
	TEST_PRINT "get's after ca_put(DBR_STRING\n");
    for (i=0; bufTypes[i]>=0; i++) {
	getType = pChan->dbrType = bufTypes[i];
	sprintf(message, "prior to ca_get (%s)", dbr_type_to_text(getType));
	CauCaDebug(message, 0);
	stat = ca_get(getType, pChan->pCh, ppVal[i]);
	CauCaDebugStat("back from ca_get", 0);
	if (stat != ECA_NORMAL)
	    TEST_PRINT "error on ca_get\n");
	if (stat == ECA_NORMAL) {
	    CauCaDebug("prior to ca_pend_io(1.0)", 0);
	    stat = ca_pend_io(1.0);
	    CauCaDebugStat("back from ca_pend_io", 0);
	    if (stat != ECA_NORMAL)
		TEST_PRINT "error on ca_get\n");
	}
	if (stat == ECA_NORMAL) {
	    cau_test_compare(pCxCmd, pCauDesc,
				i, pChan, ppVal, ppExp, ppDev, check, quiet);
	}
	else {
	    TEST_PRINT "***%-10s %20s\n", dbr_type_to_text(getType), name);
	    TEST_PRINT "***%s\n", ca_message(stat));
	}
    }
/*-----------------------------------------------------------------------------
*    now, do a put followed by a get for each type
*----------------------------------------------------------------------------*/
    if (!quiet)
	TEST_PRINT "put/get for each type\n");
    for (i=0; bufTypes[i]>=0; i++) {
	putType = bufTypes[i];
	sprintf(message, "prior to ca_put (%s)", dbr_type_to_text(putType));
	CauCaDebug(message, 0);
	stat = ca_put(putType, pChan->pCh, ppVal[i]);
	CauCaDebugStat("back from ca_put", 0);
	if (stat != ECA_NORMAL)
	    TEST_PRINT "error on ca_put\n");
	if (stat == ECA_NORMAL) {
	    CauCaDebug("prior to ca_pend_io(1.0)", 0);
	    stat = ca_pend_io(1.0);
	    CauCaDebugStat("back from ca_pend_io", 0);
	    if (stat != ECA_NORMAL)
		TEST_PRINT "error on ca_put\n");
	}
	if (stat == ECA_NORMAL) {
	    sprintf(message, "prior to ca_get (%s)", dbr_type_to_text(putType));
	    CauCaDebug(message, 0);
	    stat = ca_get(putType, pChan->pCh, ppVal[i]);
	    CauCaDebugStat("back from ca_get", 0);
	    if (stat != ECA_NORMAL)
		TEST_PRINT "error on ca_get\n");
	}
	if (stat == ECA_NORMAL) {
	    CauCaDebug("prior to ca_pend_io(1.0)", 0);
	    stat = ca_pend_io(1.0);
	    CauCaDebugStat("back from ca_pend_io(1.0)", 0);
	    if (stat != ECA_NORMAL)
		TEST_PRINT "error on ca_get\n");
	}
	if (stat == ECA_NORMAL) {
	    cau_test_compare(pCxCmd, pCauDesc,
				i, pChan, ppVal, ppExp, ppDev, check, quiet);
	}
	else {
	    TEST_PRINT "***%-10s %20s\n", dbr_type_to_text(putType), name);
	    TEST_PRINT "***%s\n", ca_message(stat));
	}
    }
    return;
testError:
    (void)printf("you must specify a channel and a value\n");
}

/*+/internal******************************************************************
* NAME	cau_test_compare - compare test values
*
*-*/
static
cau_test_compare(pCxCmd, pCauDesc, indx, pChan,ppAct,ppExp,ppDev, check, quiet)
CX_CMD	*pCxCmd;	/* pointer to command context */
CAU_DESC *pCauDesc;	/* pointer to cau descriptor */
int	indx;		/* index into the type and value arrays */
CAU_CHAN *pChan;	/* channel pointer */
void	*ppAct[];	/* pointer to array of actual values */
void	*ppExp[];	/* pointer to array of expected values */
void	*ppDev[];	/* pointer to array of allowed deviations */
int	check;
int	quiet;
{
    char	*name=ca_name(pChan->pCh);
    chtype	type=bufTypes[indx];
    char	*typeText=dbr_type_to_text(type);
    int		i;

    if (type == DBR_STRING) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %s\n", typeText, name, (char *)ppAct[indx]);
	if (check) {
	    i = strlen((char *)ppExp[indx]);
	    if (strncmp((char *)ppAct[indx], (char *)ppExp[indx], i) != 0) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:\"%s\"\n", (char *)ppExp[indx]);
		TEST_PRINT "***actual:\"%s\"\n", (char *)ppAct[indx]);
	    }
	}
    }
    else if (type == DBR_SHORT || type == DBR_ENUM) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %d\n",typeText,name, *(short *)ppAct[indx]);
	if (check) {
	    if (*(short *)ppAct[indx] != *(long *)ppExp[indx]) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:%d   actual:%d\n", 
			*(long *)ppExp[indx], *(short *)ppAct[indx]);
	    }
	}
    }
    else if (type == DBR_FLOAT) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %f\n",typeText,name, *(float *)ppAct[indx]);
	if (check) {
	    if (CAU_ABS(*(float *)ppAct[indx] - *(double *)ppExp[indx]) >
						    *(double *)ppDev[indx]) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:%f   actual:%f\n", 
			*(double *)ppExp[indx], *(float *)ppAct[indx]);
	    }
	}
    }
    else if (type == DBR_CHAR) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %d\n",typeText,name, *(UCHAR *)ppAct[indx]);
	if (check) {
	    if (*(UCHAR *)ppAct[indx] != *(long *)ppExp[indx]) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:%d   actual:%d\n", 
			*(long *)ppExp[indx], *(UCHAR *)ppAct[indx]);
	    }
	}
    }
    else if (type == DBR_LONG) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %d\n",typeText,name, *(long *)ppAct[indx]);
	if (check) {
	    if (*(long *)ppAct[indx] != *(long *)ppExp[indx]) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:%d   actual:%d\n", 
			*(long *)ppExp[indx], *(long *)ppAct[indx]);
	    }
	}
    }
    else if (type == DBR_DOUBLE) {
	if (!quiet)
	    TEST_PRINT "%-10s %20s %f\n",typeText,name, *(double *)ppAct[indx]);
	if (check) {
	    if (CAU_ABS(*(double *)ppAct[indx] - *(double *)ppExp[indx]) >
						    *(double *)ppDev[indx]) {
		TEST_PRINT "***%s:%s compare error\n", name, typeText);
		TEST_PRINT "***expected:%f   actual:%f\n", 
			*(double *)ppExp[indx], *(double *)ppAct[indx]);
	    }
	}
    }
}
  *testGet    [chanName [chanName ...]]\n\
  *test[,quiet] chanName value [intChk fltChk fltDev] (or \"value\")\n\
/*-----------------------------------------------------------------------------
* help info--test command information
*----------------------------------------------------------------------------*/
    helpTopicAdd(&pCxCmd->helpList, &helpTest, "test", "\n\
The test and testGet commands provide for exercising the data conversion\n\
feature of Channel Access.\n\
\n\
testGet does a ca_get for all simple DBR_xxx types, printing the resultant\n\
value for each.\n\
\n\
   testGet [chanName[,chanName ...]]\n\
\n\
test does a ca_put with DBR_STRING of the value specified in the command.\n\
Then a ca_get for all simple DBR_xxx types is done, storing the values\n\
internally.  Finally, for each internal value, a ca_put is done with\n\
the corresponding DBR_xxx type, a ca_get is done for the same DBR_xxx\n\
type, and the resultant value is compared with the internal value.  If\n\
the comparison fails, an error message is printed.\n\
\n\
  *test[,quiet] chanName value [intChk fltChk fltDev] (or \"value\")\n\
\n\
If ,quiet is specified, then all output is suppressed, except for error\n\
messages.  If the `check values' are specified, then the internal values\n\
obtained by the initial ca_get's are checked; for DBR_xxx types which\n\
result in a floating value, the internal value must differ from `fltChk'\n\
by no more than `fltDev'.\n\
");
