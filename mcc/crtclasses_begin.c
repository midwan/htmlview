#include <exec/types.h>

typedef void (*func_ptr)(void);

/* 
 * The head of the constructor list.
 * Initialized to -1 to indicate the "New Format" (or count sentinel).
 * Placed at the very beginning of the .ctors section.
 */
func_ptr __CTOR_LIST__[1] __attribute__((used, section(".ctors"))) = { (func_ptr) -1 };
func_ptr __DTOR_LIST__[1] __attribute__((used, section(".dtors"))) = { (func_ptr) -1 };
