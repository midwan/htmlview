/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 HTMLview class Support Site:  http://www.sf.net/projects/htmlview-mcc/

 $Id$

***************************************************************************/

// borrowed from clib2 to be able to intermix C and C++ code with the AmigaOS3 g++ 2.95.3

#include <exec/types.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdlib.h>

#if defined(__amigaos4__)
static void (*__CTOR_LIST__[1]) (void) __attribute__(( used, section(".ctors"), aligned(sizeof(void (*)(void))) ));
static void (*__DTOR_LIST__[1]) (void) __attribute__(( used, section(".dtors"), aligned(sizeof(void (*)(void))) ));
#endif

// C-runtime specific constructor/destructor
// initialization routines.
#if defined(__MORPHOS__)
#include "libnix.c"
#include "constructors.h"
#endif

#if defined(__amigaos3__)
typedef void (*func_ptr)(void);

/****************************************************************************/

// libnix handles init/exit via these symbols, which might be functions or lists depending on the startup code.
// In this shared library environment with -nostartfiles, the linker script or libnix setup seems to provide
// a code thunk (trampoline) at __INIT_LIST__, not a list of pointers.
// Therefore, we must CALL it, not iterate it.

extern void __INIT_LIST__(void);
extern void __INIT_LIST__(void);
extern void __EXIT_LIST__(void);

extern func_ptr __CTOR_LIST__[];
extern func_ptr __DTOR_LIST__[];


// void exit(int s) { (void)s; while(1); }


/****************************************************************************/

/* Safe iteration of null-terminated INIT list */
static void call_init_functions(void)
{
	/* __INIT_LIST__ is not valid in this environment (points to VTable). */
	/* Do nothing. */
}

/****************************************************************************/

/* Safe iteration of null-terminated EXIT list */
static void call_exit_functions(void)
{
	/* __EXIT_LIST__ is legacy/stabs. Not used. */
	// __EXIT_LIST__();
}

/****************************************************************************/

static void call_constructors(void)
{
	/* Handle both count-prefixed (Old GCC) and -1 terminated (New GCC) formats */
	if ((long)__CTOR_LIST__[0] == -1)
	{
		/* New format: -1, p1, p2, ... 0. Walk to end, then backwards. */
		ULONG i = 1;
		while (__CTOR_LIST__[i]) i++;
		

		/* i is now at the 0 terminator. Run backwards until index 1. */
		while (i > 1) {
			i--;
			if (__CTOR_LIST__[i]) {
				__CTOR_LIST__[i]();
			}
		}
	}
	else
	{
		/* Old format: count, p1, p2... */
		ULONG num_ctors = (ULONG)__CTOR_LIST__[0];
		ULONG i;


		/* Call all constructors in reverse order */
		for(i = 0 ; i < num_ctors ; i++)
		{
			__CTOR_LIST__[num_ctors - i]();
		}
	}
}

/****************************************************************************/

static void call_destructors(void)
{
	if ((long)__DTOR_LIST__[0] == -1)
	{
		/* New format: -1, p1, p2... 0. Run forwards? 
		   Destructors usually run in reverse of constructors? 
		   Standard is: Constructors run End->Start. Destructors run Start->End.
		   Let's assume Start->End (1..n) for dtors.
		*/
		ULONG i = 1;
		while (__DTOR_LIST__[i]) {
			__DTOR_LIST__[i]();
			i++;
		}
	}
	else
	{
		ULONG num_dtors = (ULONG)__DTOR_LIST__[0];
		static ULONG i; /* Start at 0 */

		/* Call all destructors in forward order */
		while(i++ < num_dtors)
		{
			__DTOR_LIST__[i]();
		}
	}
}
#endif

void _init(void)
{
	#if defined(__amigaos4__)
	int num_ctors,i;
	int j;

	for(i = 1, num_ctors = 0 ; __CTOR_LIST__[i] != NULL ; i++)
		num_ctors++;

	for(j = 0 ; j < num_ctors ; j++)
		__CTOR_LIST__[num_ctors - j]();
	#elif defined(__amigaos3__)
    
    /* Initialize memory pool BEFORE calling C++ constructors 
       This prevents crashes in constructors that use new/malloc */
    {
        extern void InitMemoryPool(void);
        InitMemoryPool();
    }

	call_init_functions();
	call_constructors();
	#elif defined(__MORPHOS__)
	run_constructors();
	#endif
}

/****************************************************************************/

void _fini(void)
{
	#if defined(__amigaos4__)
	int num_dtors,i;
	static int j;

	for(i = 1, num_dtors = 0 ; __DTOR_LIST__[i] != NULL ; i++)
		num_dtors++;

	while(j++ < num_dtors)
		__DTOR_LIST__[j]();
	#elif defined(__amigaos3__)
	call_destructors();
	call_exit_functions();

    /* Cleanup memory pool AFTER all C++ destructors run */
    {
        extern void CleanupMemoryPool(void);
        CleanupMemoryPool();
    }
	#elif defined(__MORPHOS__)
	run_destructors();
	#endif
}
