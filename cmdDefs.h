#ifndef INC_cmdDefs_h
#define INC_cmdDefs_h
/*	$Id$
 *	Author:	Roger A. Cole
 *	Date:	10/24/90
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
 * .00	10/24/90	rac	initial version
 * .01	06-19-91	rac	installed in SCCS
 * .02	12-05-91	rac	no longer use LWP
 *
 */
/*+/mod***********************************************************************
* TITLE	cmdDefs.h - definitions for command processing routines
*
* DESCRIPTION
*	This file contains some definitions which are needed to use
*	cmdSubr.c, a set of routines which form a sort of "front end"
*	for processing text commands.
*
* REQUIRES:
*	<genDefs.h>
*
* SEE ALSO
*	cmdSubr.c
*
*
*-***************************************************************************/
/*/subhead CX_CMD--------------------------------------------------------------
* CX_CMD
*
*	The context block for command processing routines contains items for:
*	o   assisting in commands parsing
*	o   managing generic help information
*	o   directing input and output related to commands
*----------------------------------------------------------------------------*/
typedef struct cxCmd {
    HELP_LIST	helpList;	/* list of help topics */
    HELP_TOPIC	helpBg;		/* help info--bg command */
    HELP_TOPIC	helpCmds;	/* generic command information for help */
    HELP_TOPIC	helpCmdsSpec;	/* commands--program specific */
    HELP_TOPIC	helpUsage;	/* usage information for help */
    FILE	*input;		/* input stream */
    char	*inputName;	/* name of input stream (for file input) */
    int		inputEOF;	/* 1 indicates EOF on keyboard/socket input */
    FILE	*dataOut;	/* data output stream */
    int		dataOutRedir;	/* 1 indicates dataOut is re-directed */
    char	*prompt;	/* prompt string */
    int		promptFlag;	/* 1 indicates a prompt is needed */
    char	*pLine;		/* pointer into line, used by nextXxxField */
    char	*pCommand;	/* pointer to command field */
    char	*pField;	/* pointer to next field */
    struct cxCmd *pCxCmdRoot;	/* pointer to root command context */
    struct cxCmd *pPrev;	/* pointer to previous command context */
    char	line[80];	/* input line */
    int		fldLen;		/* length of next field, including delim */
    char	delim;		/* delimiter of next field */
} CX_CMD;

/*/subhead prototypes----------------------------------------------------------
* prototypes
*----------------------------------------------------------------------------*/
long cmdBgCheck();
char *cmdRead();
void cmdSource();
void cmdCloseContext();
void cmdInitContext();

#endif
