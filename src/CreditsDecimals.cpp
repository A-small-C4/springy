#include <HouseClass.h>
#include <CCINIClass.h>
#include <Helpers/Macro.h>

#include <cmath>
#include <cstdlib>

// The [<HouseName> House] section's Credits value is read as an integer and then
// multiplied by 100 by the engine, so a fractional value such as
// Credits=123.45 has its decimals truncated and the starting money can only ever
// be a multiple of 100. Parse the raw value as a double instead to keep up to
// two decimal places; whole numbers still produce exactly the same result.
DEFINE_HOOK(0x500BD0, HouseClass_Read_INI_StartingCreditsDecimals, 0x6)
{
	enum { SkipVanillaStore = 0x500BD6 };

	GET(HouseClass*, pThis, EBX);
	GET(CCINIClass*, pINI, ESI);
	GET_STACK(const char*, pSection, 0x0);

	char buffer[0x40];
	pINI->ReadString(pSection, "Credits", "", buffer, sizeof(buffer));

	const double credits = std::strtod(buffer, nullptr);
	pThis->StartingCredits = static_cast<int>(std::lround(credits * 100.0));

	return SkipVanillaStore;
}
