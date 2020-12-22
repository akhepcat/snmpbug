#!/bin/bash

NAME = snmpbug
OBJ = $(NAME).o mib.o globals.o protocol.o utils.o
LIBS = 
CFLAGS = -W -Wall -Wextra -std=gnu99 -g -O2

$(NAME):: $(OBJ)
	cc -o $(NAME) $(OBJ) $(LIBS)
#	strip $(NAME)

clean::
	@echo "cleaning intermediate files..."
	-@rm -f $(OBJ) *~


realclean::
	@echo "removing intermediate and runtime files..."
	-@rm -f $(OBJ) $(NAME) *~
