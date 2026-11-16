//
// main.cpp - SBusSound bare metal
//
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
	// sem se nevraci - nektere destruktory v CKernel nejsou implementovane
	CKernel Kernel;
	if (!Kernel.Initialize ())
	{
		halt ();
		return EXIT_HALT;
	}

	Kernel.Run ();

	halt ();
	return EXIT_HALT;
}
