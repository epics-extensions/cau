ADD_ON = ../..
include $(ADD_ON)/src/admin/CONFIG

GCC = $(oldCC)
CC = $(oldCC)

SRCS = cau.c


OBJS = cau.o

PROD = cau

include $(ADD_ON)/src/admin/RULES

cau : $(OBJS) $(DEP_LIBS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
