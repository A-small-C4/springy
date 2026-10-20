#include <CCINIClass.h>

#include <cstdio>

#include "Config.h"
#include "Profiler.h"

namespace Springy
{
	bool Enabled = false;
	int Rate = 55;
	int Amount = 40;
	int Widen = 25;
	int Narrow = 25;
	int Stretch = 40;
	bool Profile = false;
	bool OnlyOwner = true;
	bool Shadows = true;
	bool SHP = true;
	char Sound[0x20] = "";
	int ButtonOffsetX = 0;
	int ButtonOffsetY = 0;
	char ButtonSHPAllied[0x40] = "button12_al.shp";
	char ButtonSHPSoviet[0x40] = "button12_so.shp";
	char ButtonSHPYuri[0x40] = "";

	int CategoryRate[6] = { 55, 55, 55, 55, 55, 55 };
	int CategoryAmount[6] = { 40, 40, 40, 40, 40, 40 };
	int CategoryWiden[6] = { 25, 25, 25, 25, 25, 25 };
	int CategoryNarrow[6] = { 25, 25, 25, 25, 25, 25 };
	int CategoryStretch[6] = { 40, 40, 40, 40, 40, 40 };

	void Load()
	{
		static bool loaded = false;

		if (loaded)
			return;

		loaded = true;

		auto& ini = CCINIClass::INI_RA2MD;
		const char* section = "springy";

		Enabled = ini.ReadBool(section, "Enabled", false);
		Rate = ini.ReadInteger(section, "Rate", 55);
		Amount = ini.ReadInteger(section, "Amount", 40);
		Widen = ini.ReadInteger(section, "Widen", 25);
		Narrow = ini.ReadInteger(section, "Narrow", Widen);
		Stretch = ini.ReadInteger(section, "Stretch", Amount);
		Profile = ini.ReadBool(section, "Profile", false);
		OnlyOwner = ini.ReadBool(section, "OnlyOwner", true);
		Shadows = ini.ReadBool(section, "Shadows", true);
		SHP = ini.ReadBool(section, "SHP", true);
		ini.ReadString(section, "Sound", "", Sound);
		ButtonOffsetX = ini.ReadInteger(section, "Button.OffsetX", 0);
		ButtonOffsetY = ini.ReadInteger(section, "Button.OffsetY", 0);
		ini.ReadString(section, "Button.SHP.Allied", ButtonSHPAllied, ButtonSHPAllied);
		ini.ReadString(section, "Button.SHP.Soviet", ButtonSHPSoviet, ButtonSHPSoviet);
		ini.ReadString(section, "Button.SHP.Yuri", ButtonSHPSoviet, ButtonSHPYuri);

		static const char* const names[] =
		{
			"Infantry", "Vehicle", "Aircraft", "BuildingSHP", "BuildingVXL", "Other"
		};

		for (int i = 0; i < 6; ++i)
		{
			char key[0x40];
			sprintf_s(key, "%s.Rate", names[i]);
			CategoryRate[i] = ini.ReadInteger(section, key, Rate);
			sprintf_s(key, "%s.Amount", names[i]);
			CategoryAmount[i] = ini.ReadInteger(section, key, Amount);
			sprintf_s(key, "%s.Widen", names[i]);
			CategoryWiden[i] = ini.ReadInteger(section, key, Widen);
			sprintf_s(key, "%s.Narrow", names[i]);
			CategoryNarrow[i] = ini.ReadInteger(section, key, CategoryWiden[i]);
			sprintf_s(key, "%s.Stretch", names[i]);
			CategoryStretch[i] = ini.ReadInteger(section, key, CategoryAmount[i]);
		}

		Profiler::SetEnabled(Profile);
	}
}
