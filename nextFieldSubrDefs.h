/*	$Id$ */

#ifndef INCLnextFieldSubrDefsh
#define INCLnextFieldSubrDefsh

int nextAlphField(char **ppText,char **ppField,char *pDelim);
int nextAlph1UCField(char **ppText,char **ppField,char *pDelim);
int nextANField(char **ppText,char **ppField,char *pDelim);
int nextChanNameField(char **ppText,char **ppField,char *pDelim);
int nextFltField(char **ppText,char **ppField,char *pDelim);
int nextFltFieldAsDbl(char **ppText,double *pDblVal,char *pDelim);
int nextIntField(char **ppText,char **ppField,char *pDelim);
int nextIntFieldAsInt(char **ppText,int *pIntVal,char *pDelim);
int nextIntFieldAsLong(char **ppText,long *pLongVal,char *pDelim);
int nextNonSpace(char **ppText);
int nextNonSpaceField(char **ppText,char **ppField,char *pDelim);

#endif
