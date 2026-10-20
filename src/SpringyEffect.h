#pragma once

#include <GeneralDefinitions.h>

class TechnoClass;
class HouseClass;

// Standalone "everything springs" easter egg runtime state.
namespace SpringyEffect
{
	extern bool Active;
	extern HouseClass* EnabledBy;

	// Bumped whenever the effect is switched on or off, so consumers that keep
	// cached per-object state (SpringySHP) know to drop it.
	extern int Generation;

	// Resolved deformation parameters for one object: per-TechnoType override,
	// then category default, then the global value. isVoxel tells a building
	// whether the part being drawn is a voxel (body / turret / barrel) or an SHP.
	struct Params
	{
		int Rate;
		int Amount;  // percent, 0-90: shorten at the wide/short extreme
		int Widen;   // percent, 0-90: widen at the wide/short extreme
		int Narrow;  // percent, 0-90: narrow at the tall/thin extreme
		int Stretch; // percent, 0-90: height gain at the tall/thin extreme
	};

	Params GetParams(TechnoClass* pTechno, bool isVoxel = false);

	// Number of discrete deformation steps the spring cycles through: one full
	// wave (rest -> narrower/taller -> rest -> wider/shorter -> rest).
	constexpr int Steps = 8;

	// Signed wave for a phase step (0..Steps-1): negative = narrower and taller
	// (Narrow/Stretch apply), positive = wider and shorter (Widen/Amount apply),
	// 0 = at rest.
	double WavePulse(int step);

	bool ShouldSpring(TechnoClass* pTechno);
	void Toggle();
	void Reset();
	void StartMusic();
	void StopMusic();
}

// Implemented in SpringySHP.cpp: force every object the effect was deforming to
// repaint its vanilla art. Must be called while Active is still set (the owner
// and object filters depend on it), i.e. just before switching the effect off.
namespace SpringySHP
{
	void RestoreAll();
}
