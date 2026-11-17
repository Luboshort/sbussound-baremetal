#
# SBusSound bare metal - Makefile
#
CIRCLEHOME ?= ../circle

OBJS	= main.o kernel.o wav.o mixer.o hlasy.o prevodovka.o matika.o sbus.o ini.o nastaveni.o mapovani.o

LIBS	= $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

INCLUDE += -I $(CIRCLEHOME)/addon

-include $(DEPS)
