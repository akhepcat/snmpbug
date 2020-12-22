#!/bin/bash

NAME = snmpbug
OBJ = $(NAME).o globals.o protocol.o utils.o
LIBS = 
CFLAGS = -W -Wall -Wextra -std=gnu99 -g -O2

$(NAME):: $(OBJ)
	cc -o $(NAME) $(OBJ) $(LIBS)

prod::	$(NAME) clean
	strip $(NAME)

clean::
	@echo "cleaning intermediate files..."
	-@rm -f $(OBJ) *~


realclean::
	@echo "removing intermediate and runtime files..."
	-@rm -f $(OBJ) $(NAME) *~
