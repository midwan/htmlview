#/***************************************************************************
#
# HTMLview.mcc - HTMLview MUI Custom Class
# Copyright (C) 1997-2000 Allan Odgaard
# Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# HTMLview class Support Site:  http://www.sf.net/projects/htmlview-mcc/
#
# $Id: rev.h,v 1.4 2005/04/11 03:04:38 tactica Exp $
#
#***************************************************************************/

###########################################################################
# This makefile is a very generic one. It tries to identify both, the host
# and the target operating system for which YAM should be compiled.
# However, this auto-detection can be easily overridden by directly
# specifying an option on the commandline while calling 'make'.
#
# Example:
#
# # to explicitly compile for AmigaOS3
# > make OS=os3
#
# # to compile for AmigaOS4 but with debugging
# > make OS=os4 DEBUG=
#

#############################################
# find out the HOST operating system
# on which this makefile is run
ifndef (HOST)
  HOST = $(shell uname)
  ifeq ($(HOST), AmigaOS)
    ifeq ($(shell uname -m), powerpc)
      HOST = AmigaOS4
    endif
    ifeq ($(shell uname -m), ppc)
      HOST = AmigaOS4
    endif
  endif
endif

#############################################
# now we find out the target OS for
# which we are going to compile YAM in case
# the caller didn't yet define OS himself
ifndef (OS)
  ifeq ($(HOST), AmigaOS4)
    OS = os4
  else
  ifeq ($(HOST), AmigaOS)
    OS = os3
  else
  ifeq ($(HOST), MorphOS)
    OS = mos
  else
  ifeq ($(HOST), Aros)
    OS = aros
  else
    OS = os4
  endif
  endif
  endif
  endif
endif

#############################################
# define common commands we use in this
# makefile. Please note that each of them
# might be overridden on the commandline.

# common commands
FLEX  = flex
FC    = flexcat
EXPR  = expr
CHMOD = chmod
SED   = sed
DATE  = date
RM    = rm -f
RMDIR = rm -rf
MKDIR = mkdir

# override commands for native builds
ifeq ($(HOST), AmigaOS4)
  # AmigaOS4 host
  RM    = delete force
  RMDIR = delete force all
  MKDIR = makedir
  DATE  = gdate
else
ifeq ($(HOST), AmigaOS)
  # AmigaOS3 host
  RM    = delete force
  RMDIR = delete force all
  MKDIR = makedir
else
ifeq ($(HOST), MorphOS)
  # MorphOS host
  RM    = delete force
  RMDIR = delete force all
  MKDIR = makedir
endif
endif
endif

###########################################################################
# CPU and DEBUG can be defined outside, defaults to above
# using e.g. "make DEBUG= CPU=-mcpu=603e" produces optimized non-debug
# PPC-603e version
#
# OPTFLAGS are disabled by DEBUG normally!
#
# ignored warnings are:
# none - because we want to compile with -Wall all the time

# Common Directories
OBJDIR = .obj_$(OS)
BINDIR = bin_$(OS)
LOCALE = locale

# target definition
MCCTARGET = $(BINDIR)/HTMLview.mcc
TESTTARGET= $(BINDIR)/HTMLview-Test
SIMPLETARGET= $(BINDIR)/SimpleTest
LIBLOADTARGET= $(BINDIR)/LibLoad_Test

# Common compiler/linker flags
WARN     = -W -Wall -Wwrite-strings
OPTFLAGS = -O3 -fomit-frame-pointer
DEBUG    =
DEBUGSYM =

ifneq ($(DEBUG),)
  OPTFLAGS = -O0
  DEBUGSYM = -g -gstabs
  CFLAGS   = -I. -I../mcp -I../include $(CPU) $(WARN) $(OPTFLAGS) \
             -DDEBUG $(DEBUGSYM) -c
else
  CFLAGS   = -I. -I../mcp -I../include $(CPU) $(WARN) $(OPTFLAGS) \
             -DNDEBUG -c
endif
LDFLAGS  = $(CPU) $(DEBUGSYM)
LDLIBS   =

# Per-program extras; populated by the AmiSSL block below.
TEST_CFLAGS =
TEST_LDLIBS =
TEST_DEPS   =

# different options per target OS
ifeq ($(OS), os4)

  ##############################
  # AmigaOS4

  # Compiler/link/strip commands
  CC      = ppc-amigaos-gcc
  CXX     = ppc-amigaos-g++
  STRIP   = ppc-amigaos-strip
  OBJDUMP = ppc-amigaos-objdump

  # Compiler/Linker flags
  CRT      = newlib
  CPU      = -mcpu=powerpc
  WARN     += -Wno-unused-parameter -Wno-narrowing -Wno-sign-compare
  CFLAGS   += -mcrt=$(CRT) -D__USE_INLINE__ -D__NEW_TIMEVAL_DEFINITION_USED__ \
              -D__USE_CLASSIC_MINTERM__ -Wa,-mregnames -DNO_PPCINLINE_STDARG \
              -fno-exceptions -fno-threadsafe-statics
  LDFLAGS  += -mcrt=$(CRT) -nostartfiles -Wl,-u,_startu
  LDLIBS   += -lauto

  # additional object files required
  M68KSTUBS = $(OBJDIR)/mccclass_68k.o

else
ifeq ($(OS), os3)

  ##############################
  # AmigaOS3

  # Compiler/link/strip commands
  CC      = m68k-amigaos-gcc
  CXX     = m68k-amigaos-g++
  STRIP   = m68k-amigaos-strip
  OBJDUMP = m68k-amigaos-objdump

  # Compiler/Linker flags
  CPU	  = -m68020-60 -msoft-float
  WARN    += -Wno-sign-compare
  CFLAGS  += -noixemul -DNO_INLINE_STDARG -D__amigaos3__
  LDFLAGS += -noixemul -nostartfiles
  LDLIBS  += -lmui
  ifneq ($(DEBUG),)
    LDLIBS += -ldebug
  endif


else
ifeq ($(OS), mos)

  ##############################
  # MorphOS

  # Compiler/link/strip commands
  CC      = ppc-morphos-gcc
  CXX     = ppc-morphos-g++
  STRIP   = ppc-morphos-strip
  OBJDUMP = ppc-morphos-objdump

  # Compiler/Linker flags
  CPU     = -mcpu=powerpc
  CFLAGS  += -noixemul -DNO_PPCINLINE_STDARG
  LDFLAGS += -noixemul -nostartfiles -shared
  ifneq ($(DEBUG),)
    LDLIBS += -ldebug
  endif


else
ifeq ($(OS), aros)

  ##############################
  # AROS

  # Compiler/link/strip commands
  CC      = i686-aros-gcc
  CXX     = i686-aros-g++
  STRIP   = i686-aros-strip
  OBJDUMP = i686-aros-objdump

  # Compiler/Linker flags
  CPU     =

endif
endif
endif
endif

###########################################################################
# AmiSSL SDK — enables HTTPS in SimpleTest / LibLoad_Test.
#
# The SDK is downloaded from jens-maus/amissl on first build. Pass
# USE_AMISSL=0 to disable; the test programs then speak plain HTTP only.
# No SDK is distributed for MorphOS, so USE_AMISSL is forced off there.

AMISSL_VERSION ?= 5.27
AMISSL_SDK_DIR ?= amissl_sdk
AMISSL_SDK_READY = $(AMISSL_SDK_DIR)/.ready
AMISSL_INC = $(AMISSL_SDK_DIR)/AmiSSL/Developer/include
AMISSL_URL = https://github.com/jens-maus/amissl/releases/download/$(AMISSL_VERSION)/AmiSSL-$(AMISSL_VERSION)-SDK.lha

ifeq ($(OS), os3)
  AMISSL_LIBDIR = $(AMISSL_SDK_DIR)/AmiSSL/Developer/lib/AmigaOS3
  USE_AMISSL ?= 1
else ifeq ($(OS), os4)
  AMISSL_LIBDIR = $(AMISSL_SDK_DIR)/AmiSSL/Developer/lib/AmigaOS4/newlib
  USE_AMISSL ?= 1
else
  USE_AMISSL = 0
endif

ifeq ($(USE_AMISSL), 1)
  # Headers only: the inline macros in <inline/amissl.h> dispatch through
  # a task-local library base, so no stub lib (libamisslstubs.a) is needed
  # and there are no global AmiSSLBase / AmiSSLMasterBase symbols to
  # satisfy at link time.
  TEST_CFLAGS += -DHAVE_AMISSL -I$(AMISSL_INC)
  TEST_DEPS   += $(AMISSL_SDK_READY)
endif

$(AMISSL_SDK_READY):
	@echo "  GET   AmiSSL SDK $(AMISSL_VERSION)"
	@mkdir -p $(AMISSL_SDK_DIR)
	@cd $(AMISSL_SDK_DIR) && curl -fsSL -o sdk.lha "$(AMISSL_URL)"
	@cd $(AMISSL_SDK_DIR) && lha xq sdk.lha && rm -f sdk.lha
	@touch $@

###########################################################################
# Here starts all stuff that is common for all target platforms and
# hosts.

MCCOBJS = \
          $(OBJDIR)/crtclasses_begin.o \
          $(OBJDIR)/library.o \
          $(OBJDIR)/library_cpp.o \
          $(OBJDIR)/ctor_dtor.o \
          $(OBJDIR)/classes/AClass.o \
          $(OBJDIR)/classes/AreaClass.o \
          $(OBJDIR)/classes/AttrClass.o \
          $(OBJDIR)/classes/BackFillClass.o \
          $(OBJDIR)/classes/BaseClass.o \
          $(OBJDIR)/classes/BlockquoteClass.o \
          $(OBJDIR)/classes/BodyClass.o \
          $(OBJDIR)/classes/BrClass.o \
          $(OBJDIR)/classes/CommentClass.o \
          $(OBJDIR)/classes/DDClass.o \
          $(OBJDIR)/classes/DLClass.o \
          $(OBJDIR)/classes/DTClass.o \
          $(OBJDIR)/classes/DummyClass.o \
          $(OBJDIR)/classes/FontClass.o \
          $(OBJDIR)/classes/FontStyleClass.o \
          $(OBJDIR)/classes/FormClass.o \
          $(OBJDIR)/classes/FrameClass.o \
          $(OBJDIR)/classes/FramesetClass.o \
          $(OBJDIR)/classes/HRClass.o \
          $(OBJDIR)/classes/HostClass.o \
          $(OBJDIR)/classes/ImgClass.o \
          $(OBJDIR)/classes/InputClass.o \
          $(OBJDIR)/classes/IsIndexClass.o \
          $(OBJDIR)/classes/LIClass.o \
          $(OBJDIR)/classes/MapClass.o \
          $(OBJDIR)/classes/MetaClass.o \
          $(OBJDIR)/classes/NoFramesClass.o \
          $(OBJDIR)/classes/OLClass.o \
          $(OBJDIR)/classes/OptionClass.o \
          $(OBJDIR)/classes/PClass.o \
          $(OBJDIR)/classes/ScriptClass.o \
          $(OBJDIR)/classes/SelectClass.o \
          $(OBJDIR)/classes/SuperClass.o \
          $(OBJDIR)/classes/TDClass.o \
          $(OBJDIR)/classes/THClass.o \
          $(OBJDIR)/classes/TRClass.o \
          $(OBJDIR)/classes/TableClass.o \
          $(OBJDIR)/classes/TextAreaClass.o \
          $(OBJDIR)/classes/TextClass.o \
          $(OBJDIR)/classes/TitleClass.o \
          $(OBJDIR)/classes/TreeClass.o \
          $(OBJDIR)/classes/ULClass.o \
          $(OBJDIR)/AllocateColours.o \
          $(OBJDIR)/Animation.o \
          $(OBJDIR)/Classes.o \
          $(OBJDIR)/Colours.o \
          $(OBJDIR)/Config.o \
          $(OBJDIR)/Dispatcher.o \
          $(OBJDIR)/Entities.o \
          $(OBJDIR)/Forms.o \
          $(OBJDIR)/GetSetAttrs.o \
          $(OBJDIR)/HandleMUIEvent.o \
          $(OBJDIR)/IM_ColourManager.o \
          $(OBJDIR)/IM_Dither.o \
          $(OBJDIR)/IM_Output.o \
          $(OBJDIR)/IM_Render.o \
          $(OBJDIR)/IM_Scale.o \
          $(OBJDIR)/ImageManager.o \
          $(OBJDIR)/Layout.o \
          $(OBJDIR)/Mark.o \
          $(OBJDIR)/Memory.o \
          $(OBJDIR)/MinMax.o \
          $(OBJDIR)/ParseMessage.o \
          $(OBJDIR)/ParseThread.o \
          $(OBJDIR)/Render.o \
          $(OBJDIR)/ScanArgs.o \
          $(OBJDIR)/ScrollGroup.o \
          $(OBJDIR)/SharedData.o \
          $(OBJDIR)/TagInfo.o \
          $(OBJDIR)/TernaryTrees.o \
          $(OBJDIR)/URLHandling.o \
          $(OBJDIR)/StringClass.o \
          $(OBJDIR)/Debug.o \
          $(OBJDIR)/crtclasses_end.o

TESTOBJS= $(OBJDIR)/library_cpp.o \
          $(OBJDIR)/library_cpp.o \
          $(OBJDIR)/ctor_dtor.o \
          $(OBJDIR)/classes/AClass.o \
          $(OBJDIR)/classes/AreaClass.o \
          $(OBJDIR)/classes/AttrClass.o \
          $(OBJDIR)/classes/BackFillClass.o \
          $(OBJDIR)/classes/BaseClass.o \
          $(OBJDIR)/classes/BlockquoteClass.o \
          $(OBJDIR)/classes/BodyClass.o \
          $(OBJDIR)/classes/BrClass.o \
          $(OBJDIR)/classes/CommentClass.o \
          $(OBJDIR)/classes/DDClass.o \
          $(OBJDIR)/classes/DLClass.o \
          $(OBJDIR)/classes/DTClass.o \
          $(OBJDIR)/classes/DummyClass.o \
          $(OBJDIR)/classes/FontClass.o \
          $(OBJDIR)/classes/FontStyleClass.o \
          $(OBJDIR)/classes/FormClass.o \
          $(OBJDIR)/classes/FrameClass.o \
          $(OBJDIR)/classes/FramesetClass.o \
          $(OBJDIR)/classes/HRClass.o \
          $(OBJDIR)/classes/HostClass.o \
          $(OBJDIR)/classes/ImgClass.o \
          $(OBJDIR)/classes/InputClass.o \
          $(OBJDIR)/classes/IsIndexClass.o \
          $(OBJDIR)/classes/LIClass.o \
          $(OBJDIR)/classes/MapClass.o \
          $(OBJDIR)/classes/MetaClass.o \
          $(OBJDIR)/classes/NoFramesClass.o \
          $(OBJDIR)/classes/OLClass.o \
          $(OBJDIR)/classes/OptionClass.o \
          $(OBJDIR)/classes/PClass.o \
          $(OBJDIR)/classes/ScriptClass.o \
          $(OBJDIR)/classes/SelectClass.o \
          $(OBJDIR)/classes/SuperClass.o \
          $(OBJDIR)/classes/TDClass.o \
          $(OBJDIR)/classes/THClass.o \
          $(OBJDIR)/classes/TRClass.o \
          $(OBJDIR)/classes/TableClass.o \
          $(OBJDIR)/classes/TextAreaClass.o \
          $(OBJDIR)/classes/TextClass.o \
          $(OBJDIR)/classes/TitleClass.o \
          $(OBJDIR)/classes/TreeClass.o \
          $(OBJDIR)/classes/ULClass.o \
          $(OBJDIR)/AllocateColours.o \
          $(OBJDIR)/Animation.o \
          $(OBJDIR)/Classes.o \
          $(OBJDIR)/Colours.o \
          $(OBJDIR)/Config.o \
          $(OBJDIR)/Dispatcher.o \
          $(OBJDIR)/Entities.o \
          $(OBJDIR)/Forms.o \
          $(OBJDIR)/GetSetAttrs.o \
          $(OBJDIR)/HandleMUIEvent.o \
          $(OBJDIR)/IM_ColourManager.o \
          $(OBJDIR)/IM_Dither.o \
          $(OBJDIR)/IM_Output.o \
          $(OBJDIR)/IM_Render.o \
          $(OBJDIR)/IM_Scale.o \
          $(OBJDIR)/ImageManager.o \
          $(OBJDIR)/Layout.o \
          $(OBJDIR)/Mark.o \
          $(OBJDIR)/Memory.o \
          $(OBJDIR)/MinMax.o \
          $(OBJDIR)/ParseMessage.o \
          $(OBJDIR)/ParseThread.o \
          $(OBJDIR)/Render.o \
          $(OBJDIR)/ScanArgs.o \
          $(OBJDIR)/ScrollGroup.o \
          $(OBJDIR)/SharedData.o \
          $(OBJDIR)/TagInfo.o \
          $(OBJDIR)/TernaryTrees.o \
          $(OBJDIR)/URLHandling.o \
          $(OBJDIR)/StringClass.o \
          $(OBJDIR)/Debug.o

ifeq ($(OS), os3)
MCCOBJS += \
          $(OBJDIR)/vastubs.o

TESTOBJS += \
          $(OBJDIR)/vastubs.o
endif

ifeq ($(OS), os4)
MCCOBJS += $(M68KSTUBS)
endif

# available catalog translations
CATALOGS = $(LOCALE)/deutsch.catalog

#

# Build test programs for all platforms
all: $(BINDIR) $(OBJDIR) $(MCCTARGET) $(SIMPLETARGET) $(LIBLOADTARGET) $(BINDIR)/test.png

# make the object directories
$(OBJDIR):
	@echo "  MKDIR $@"
	@$(MKDIR) $(OBJDIR)
	@$(MKDIR) $(OBJDIR)/classes

# make the binary directories
$(BINDIR):
	@echo "  MKDIR $@"
	@$(MKDIR) $(BINDIR)

#

$(OBJDIR)/%.o: %.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

$(OBJDIR)/%.o: %.cpp
	@echo "  CXX   $<"
	@$(CXX) $(CFLAGS) $< -o $@

$(OBJDIR)/mccclass_68k.o: ../include/mccclass_68k.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Test programs get the AmiSSL include path (if enabled). The explicit
# rules below shadow the generic %.o: %.c rule so TEST_CFLAGS applies.
$(OBJDIR)/SimpleTest.o: SimpleTest.c test_image_hook.h $(TEST_DEPS)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(TEST_CFLAGS) $< -o $@

$(OBJDIR)/LibLoad_Test.o: LibLoad_Test.c test_image_hook.h $(TEST_DEPS)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(TEST_CFLAGS) $< -o $@

#

# Link against all MCC objects EXCEPT library.o (globals conflict)
LIBOBJS = $(filter-out $(OBJDIR)/library.o $(OBJDIR)/crtclasses_begin.o $(OBJDIR)/crtclasses_end.o, $(MCCOBJS))


$(SIMPLETARGET): $(OBJDIR)/SimpleTest.o
	@echo "  LD    $@"
ifneq (,$(DEBUG))
	@$(CXX) $(filter-out -nostartfiles,$(LDFLAGS)) -o $@.debug $(OBJDIR)/SimpleTest.o $(LDLIBS) $(TEST_LDLIBS) -ldebug -Wl,-Map,$@.map
else
	@$(CXX) $(filter-out -nostartfiles,$(LDFLAGS)) -o $@.debug $(OBJDIR)/SimpleTest.o $(LDLIBS) $(TEST_LDLIBS) -Wl,-Map,$@.map
endif
	@$(STRIP) --preserve-dates -R.comment -R.sdata2 -S -o $@ $@.debug

$(LIBLOADTARGET): $(OBJDIR)/LibLoad_Test.o
	@echo "  LD    $@"
ifneq (,$(DEBUG))
	@$(CXX) $(filter-out -nostartfiles,$(LDFLAGS)) -o $@.debug $(OBJDIR)/LibLoad_Test.o $(LDLIBS) $(TEST_LDLIBS) -ldebug -Wl,-Map,$@.map
else
	@$(CXX) $(filter-out -nostartfiles,$(LDFLAGS)) -o $@.debug $(OBJDIR)/LibLoad_Test.o $(LDLIBS) $(TEST_LDLIBS) -Wl,-Map,$@.map
endif
	@$(STRIP) --preserve-dates -R.comment -R.sdata2 -S -o $@ $@.debug


$(MCCTARGET) : $(MCCOBJS)
	@echo "  LD    $@"
	@$(CXX) $(LDFLAGS) -o $@.debug $(MCCOBJS) $(LDLIBS) -Wl,-Map,$@.map
	@$(STRIP) --preserve-dates -R.comment -R.sdata2 -S -o $@ $@.debug

# Copy the sample PNG used by the two test programs into the binary dir.
$(BINDIR)/test.png: testdata/test.png | $(BINDIR)
	@echo "  CP    $@"
	@cp testdata/test.png $@

$(OBJDIR)/library.o: library.c ../include/mccinit.c \
  HTMLview_mcc.h private.h rev.h

locale.h: locale.c
locale.c: locale/HTMLview_mcc.cd locale_c.sd locale_h.sd
	@echo "  GN    $@"
	@$(FC) locale/HTMLview_mcc.cd locale.h=locale_h.sd locale.c=locale_c.sd

## CATALOG GENERATION #################

$(LOCALE)/%.catalog: $(LOCALE)/%.ct
	@echo "  FC    $@"
	@$(FC) $(LOCALE)/HTMLview_mcc.cd $< CATALOG $@

.IGNORE: $(CATALOGS)

catalogs: $(CATALOGS)


#

.PHONY: clean
clean:
	-$(RM) $(MCCTARGET) $(MCCTARGET).debug $(MCCTARGET).map
	-$(RM) $(TESTTARGET) $(TESTTARGET).debug $(TESTTARGET).map
	-$(RM) $(SIMPLETARGET) $(SIMPLETARGET).debug $(SIMPLETARGET).map
	-$(RM) $(LIBLOADTARGET) $(LIBLOADTARGET).debug $(LIBLOADTARGET).map
	-$(RM) $(MCCOBJS) $(TESTOBJS) $(M68KSTUBS)

.PHONY: distclean
distclean: clean
	-$(RM) locale.?
	-$(RMDIR) $(OBJDIR)
	-$(RMDIR) $(BINDIR)
