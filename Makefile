#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
# National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
# Operator of Los Alamos National Laboratory.
# This file is distributed subject to a Software License Agreement found
# in the file LICENSE that is included with this distribution. 
#*************************************************************************
#
# $Id$
#
TOP = ../..
include $(TOP)/configure/CONFIG

USR_LIBS = ca Com

PROD_HOST = cau

cau_SRCS += cau.c
cau_SRCS += nextFieldSubr.c
cau_SRCS += cmdSubr.c
cau_SRCS += genSubr.c
cau_SRCS += helpSubr.c
cau_SRCS += cvtNumbers.c

include $(TOP)/configure/RULES

