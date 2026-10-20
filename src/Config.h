#pragma once

// Standalone configuration for springy. Read from the [springy] section of
// RA2MD.INI and, per TechnoType, from that type's section in the rules INI
// (springy.Rate/Amount/Widen/Narrow/Stretch).
namespace Springy
{
	enum class Category : int
	{
		Infantry = 0,
		Vehicle,      // units, ships included
		Aircraft,
		BuildingSHP,
		BuildingVXL,
		Other,
		Count
	};

	extern bool Enabled;
	extern int Rate;
	extern int Amount;
	extern int Widen;
	extern int Narrow;
	extern int Stretch;
	extern bool Profile;
	extern bool OnlyOwner;
	extern bool Shadows;
	extern bool SHP;
	extern char Sound[0x20];
	extern int ButtonOffsetX;
	extern int ButtonOffsetY;
	extern char ButtonSHPAllied[0x40];
	extern char ButtonSHPSoviet[0x40];
	extern char ButtonSHPYuri[0x40];

	extern int CategoryRate[static_cast<int>(Category::Count)];
	extern int CategoryAmount[static_cast<int>(Category::Count)];
	extern int CategoryWiden[static_cast<int>(Category::Count)];
	extern int CategoryNarrow[static_cast<int>(Category::Count)];
	extern int CategoryStretch[static_cast<int>(Category::Count)];

	// Idempotent; reads RA2MD.INI on first call.
	void Load();
}
