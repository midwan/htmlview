#include <exec/types.h>

typedef void (*func_ptr)(void);

/* 
 * The tail of the constructor list.
 * Initialized to 0 (NULL terminator).
 * Placed at the very end of the .ctors section.
 */
func_ptr __CTOR_END__[1] __attribute__((used, section(".ctors"))) = { (func_ptr) 0 };
func_ptr __DTOR_END__[1] __attribute__((used, section(".dtors"))) = { (func_ptr) 0 };
