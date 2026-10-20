#include "SpringyButton.h"
#include "Config.h"

#include <Helpers/Macro.h>

// Match start / end: (re)create or destroy the command bar button and clear the
// spring runtime state. Reuses the same game addresses Phobos uses for its
// sidebar button hooks.
DEFINE_HOOK(0x6A5082, SidebarClass_InitClear_springy, 0x5)
{
	SpringyButtonClass::InitClear();
	return 0;
}

DEFINE_HOOK(0x6A5839, SidebarClass_InitIO_springy, 0x5)
{
	Springy::Load();
	SpringyButtonClass::InitIO();
	return 0;
}
