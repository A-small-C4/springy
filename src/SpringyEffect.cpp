#include "SpringyEffect.h"
#include "Config.h"
#include "Profiler.h"

#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <ThemeClass.h>
#include <LocomotionClass.h>
#include <Fundamentals.h>
#include <YRMath.h>
#include <CCINIClass.h>
#include <Helpers/Macro.h>

#include <algorithm>
#include <unordered_map>

// Voxel (VXL) sprites are pre-rasterized per VoxelIndexKey and, on a cache hit,
// blitted axis-aligned, so scaling the draw matrix alone does nothing; we scale
// the matrix and fold a spring step into the key so the drawer keeps a distinct
// sprite per (orientation, step). Those sprites are then seam-filled while built
// (see the bottom of this file). The vanilla caller's "already cached" matrix
// shortcut must also be disabled while the effect is on (see
// UnitClass_DrawVXL_ForceTurretMatrixRecompute below).
//
// Injection is at the shared Techno voxel drawer TechnoClass::DrawVoxel
// (0x706640), reached by all Techno categories: [ESP+0xC] = VoxelIndexKey,
// [ESP+0x1C] = Matrix3D*, ECX = this (Techno).
//
// The looping music is played through the game's own score system (ThemeClass).

bool SpringyEffect::Active = false;
HouseClass* SpringyEffect::EnabledBy = nullptr;
int SpringyEffect::Generation = 0;

namespace SpringyEffect
{
	bool MusicPlaying = false;
}

// Seam-fill state: while the drawer rasterizes a sprite for a springing object,
// the RLE row hook fills transparent 1px interior gaps that the non-uniform
// scale leaves behind.
namespace SpringyVoxel
{
	bool DilateRows = false;

	// Set by the DrawVoxel hook for every voxel draw: true only while the
	// object being drawn is actually springing, so non-springing rasterizations
	// stay untouched even when the effect is on.
	bool CurrentVoxelSpringing = false;
}

namespace
{
	struct TypeOverride
	{
		int Rate = -1;
		int Amount = -1;
		int Widen = -1;
		int Narrow = -1;
		int Stretch = -1;
	};

	std::unordered_map<const TechnoTypeClass*, TypeOverride> TypeOverrides;

	const TypeOverride& GetTypeOverride(const TechnoTypeClass* pType)
	{
		const auto it = TypeOverrides.find(pType);

		if (it != TypeOverrides.end())
			return it->second;

		TypeOverride o;

		if (auto* pRules = CCINIClass::INI_Rules)
		{
			o.Rate = pRules->ReadInteger(pType->ID, "springy.Rate", -1);
			o.Amount = pRules->ReadInteger(pType->ID, "springy.Amount", -1);
			o.Widen = pRules->ReadInteger(pType->ID, "springy.Widen", -1);
			o.Narrow = pRules->ReadInteger(pType->ID, "springy.Narrow", -1);
			o.Stretch = pRules->ReadInteger(pType->ID, "springy.Stretch", -1);
		}

		return TypeOverrides.emplace(pType, o).first->second;
	}

	int GetCategory(TechnoClass* pTechno, bool voxelDraw)
	{
		if (!pTechno)
			return static_cast<int>(Springy::Category::Other);

		switch (pTechno->WhatAmI())
		{
		case AbstractType::Infantry:
			return static_cast<int>(Springy::Category::Infantry);
		case AbstractType::Unit:
			return static_cast<int>(Springy::Category::Vehicle);
		case AbstractType::Aircraft:
			return static_cast<int>(Springy::Category::Aircraft);
		case AbstractType::Building:
			// The type's Voxel flag only describes the main body; a building's
			// voxel turret/barrel is a sub-part. Decide by the draw path instead.
			return static_cast<int>(voxelDraw ? Springy::Category::BuildingVXL : Springy::Category::BuildingSHP);
		default:
			return static_cast<int>(Springy::Category::Other);
		}
	}
}

bool SpringyEffect::ShouldSpring(TechnoClass* pTechno)
{
	if (!Active || !pTechno)
		return false;

	Springy::Load();

	if (!Springy::OnlyOwner)
		return true;

	const auto pPlayer = EnabledBy ? EnabledBy : HouseClass::CurrentPlayer;

	if (!pPlayer)
		return false;

	if (pTechno->Owner == pPlayer)
		return true;

	if (pTechno->MindControlledBy && pTechno->MindControlledBy->Owner == pPlayer)
		return true;

	return false;
}

SpringyEffect::Params SpringyEffect::GetParams(TechnoClass* pTechno, bool isVoxel)
{
	Springy::Load();

	const int category = GetCategory(pTechno, isVoxel);

	Params params {
		std::max(Springy::CategoryRate[category], 1),
		std::clamp(Springy::CategoryAmount[category], 0, 90),
		std::clamp(Springy::CategoryWiden[category], 0, 90),
		std::clamp(Springy::CategoryNarrow[category], 0, 90),
		std::clamp(Springy::CategoryStretch[category], 0, 90)
	};

	if (pTechno)
	{
		if (const auto pType = pTechno->GetTechnoType())
		{
			const auto& o = GetTypeOverride(pType);

			if (o.Rate >= 0)
				params.Rate = std::max(o.Rate, 1);

			if (o.Amount >= 0)
				params.Amount = std::clamp(o.Amount, 0, 90);

			if (o.Widen >= 0)
				params.Widen = std::clamp(o.Widen, 0, 90);

			if (o.Narrow >= 0)
				params.Narrow = std::clamp(o.Narrow, 0, 90);

			if (o.Stretch >= 0)
				params.Stretch = std::clamp(o.Stretch, 0, 90);
		}
	}

	return params;
}

void SpringyEffect::StartMusic()
{
	if (MusicPlaying)
		return;

	Springy::Load();

	const char* pID = Springy::Sound;

	if (!pID || !pID[0])
		return;

	const int index = ThemeClass::Instance.FindIndex(pID);

	if (index < 0)
		return;

	ThemeClass::Instance.Play(index);
	MusicPlaying = true;
}

void SpringyEffect::StopMusic()
{
	if (!MusicPlaying)
		return;

	ThemeClass::Instance.Stop();
	// -2 makes the score AI resume a random normal theme.
	ThemeClass::Instance.Queue(-2);
	MusicPlaying = false;
}

void SpringyEffect::Reset()
{
	Active = false;
	EnabledBy = nullptr;
	MusicPlaying = false;
	++Generation;
}

void SpringyEffect::Toggle()
{
	Springy::Load();

	if (Active)
	{
		// Every deformed object must be repainted as its vanilla art; nothing
		// would redraw a static building on its own. Done while Active is
		// still set so the object filters still apply.
		SpringySHP::RestoreAll();
	}

	Active = !Active;

	if (Active)
	{
		EnabledBy = HouseClass::CurrentPlayer;
		StartMusic();
	}
	else
	{
		StopMusic();
		EnabledBy = nullptr;
	}

	++Generation;
	Springy::Profiler::Mark(Active ? "spring ON" : "spring OFF");
}

namespace SpringyEffect
{
	// Phase bucket (0..Steps-1) over one full wave. Also used as the per-step
	// sprite cache index, so it must stay within one Steps-sized range.
	int VoxelStep(int rate)
	{
		rate = std::max(rate, 1);

		const int bucket = static_cast<int>(
			static_cast<long long>(Unsorted::CurrentFrame % rate) * Steps / rate);

		return std::min(bucket, Steps - 1);
	}

	// Signed wave for a phase step: starts at rest, first dips to -1 (narrower
	// and taller), passes through rest and rises to +1 (wider and shorter).
	double WavePulse(int step)
	{
		static constexpr double Table[Steps] =
		{
			0.0,
			-0.70710678118654752, -1.0, -0.70710678118654752,
			0.0,
			0.70710678118654752, 1.0, 0.70710678118654752
		};

		return Table[std::clamp(step, 0, Steps - 1)];
	}

	void ApplyMatrix(Matrix3D* pMatrix, const Params& params)
	{
		if (!pMatrix)
			return;

		const double pulse = WavePulse(VoxelStep(params.Rate));

		// Select the extreme's own pair of amounts: the tall/thin half of the
		// wave reads Narrow/Stretch, the wide/short half reads Widen/Amount.
		const double horizAmount =
			static_cast<double>(pulse >= 0.0 ? params.Widen : params.Narrow) / 100.0;
		const double vertAmount =
			static_cast<double>(pulse >= 0.0 ? params.Amount : params.Stretch) / 100.0;

		const float widen = static_cast<float>(1.0 + horizAmount * pulse);
		const float height = static_cast<float>(1.0 - vertAmount * pulse);

		pMatrix->ScaleX(widen);
		pMatrix->ScaleY(widen);
		pMatrix->ScaleZ(height);
	}
}

DEFINE_HOOK(0x706640, TechnoClass_DrawVoxel_SpringyEffect, 0x5)
{
	SpringyVoxel::CurrentVoxelSpringing = false;
	++Springy::Profiler::VoxelDraws;

	if (!SpringyEffect::Active)
		return 0;

	GET(TechnoClass*, pThis, ECX);

	if (!SpringyEffect::ShouldSpring(pThis))
		return 0;

	SpringyVoxel::CurrentVoxelSpringing = true;
	++Springy::Profiler::VoxelDrawsSpringing;

	GET_STACK(Matrix3D*, pMatrix, 0x1C);
	REF_STACK(VoxelIndexKey, key, 0xC);

	const long long pT0 = Springy::Profiler::Now();

	const auto params = SpringyEffect::GetParams(pThis, true);

	if (pMatrix)
		SpringyEffect::ApplyMatrix(pMatrix, params);

	// Keep a distinct cache entry per (orientation, step) so the widened sprite
	// is cached and reused (and seam-filled while built) instead of re-rasterized
	// every frame. The step is added in a high range so it cannot collide with a
	// natural voxel key.
	const unsigned combined = 0x10000000u
		+ static_cast<unsigned>(static_cast<int>(key)) * static_cast<unsigned>(SpringyEffect::Steps)
		+ static_cast<unsigned>(SpringyEffect::VoxelStep(params.Rate));
	key = VoxelIndexKey(static_cast<int>(combined));

	Springy::Profiler::AddElapsed(Springy::Profiler::VoxelHookTicks, pT0);

	return 0;
}

// The vanilla turret/barrel drawing skips recomputing its draw matrix when its
// sprite-cache pre-check (done with the natural key) hits: it then passes a
// "don't care" matrix and relies on the blitted cached sprite. The spring folds
// the step into the key, so DrawVoxel misses that cache and would rasterize
// with the don't-care matrix - the vanilla one carries no camera pitch (the
// turret looks like it is seen from straight above), the Phobos one no turret
// offset (an offset turret sits centred). This only happens for facings drawn
// before the effect was enabled, the only ones with natural-key cache entries.
// While the effect is on, force the caller onto the full recompute path.
//
// Hooked at 0x73B9B0, where every pre-check path converges, rather than at the
// 0x73BA12 branch itself: Phobos rewrites the drawing at 0x73BA12 and its
// handler runs first, so a hook at that address is skipped in a Phobos setup.
DEFINE_HOOK(0x73B9B0, UnitClass_DrawVXL_ForceTurretMatrixRecompute, 0x6)
{
	if (!SpringyEffect::Active)
		return 0;

	GET(TechnoClass*, pThis, EBP);

	if (pThis && !SpringyEffect::ShouldSpring(pThis))
		return 0;

	R->Stack<byte>(0x11, 0); // haveTurretCache = false -> recompute the matrix

	return 0;
}

// Shadow: LocomotionClass::Shadow_Matrix (0x55A7D0): matrix at [ESP+0x24],
// key pointer at [ESP+0x90], this = EBX.
DEFINE_HOOK(0x55A862, LocomotionClass_Shadow_Matrix_SpringyEffect, 0x7)
{
	if (!SpringyEffect::Active || !Springy::Shadows)
		return 0;

	GET(LocomotionClass*, pLoco, EBX);
	LEA_STACK(Matrix3D*, pMatrix, 0x24);
	GET_STACK(VoxelIndexKey*, pKey, 0x90);

	if (!pLoco || !SpringyEffect::ShouldSpring(pLoco->Owner))
		return 0;

	if (pMatrix)
		SpringyEffect::ApplyMatrix(pMatrix, SpringyEffect::GetParams(pLoco->Owner, true));

	if (pKey)
		*pKey = VoxelIndexKey(-1);

	return 0;
}

// Aircraft shadows use FlyLocomotionClass::Shadow_Matrix (0x4CFB00), same layout.
DEFINE_HOOK(0x4CFBAB, FlyLocomotionClass_Shadow_Matrix_SpringyEffect, 0x7)
{
	if (!SpringyEffect::Active || !Springy::Shadows)
		return 0;

	GET(LocomotionClass*, pLoco, EBX);
	LEA_STACK(Matrix3D*, pMatrix, 0x24);
	GET_STACK(VoxelIndexKey*, pKey, 0x90);

	if (!pLoco || !SpringyEffect::ShouldSpring(pLoco->Owner))
		return 0;

	if (pMatrix)
		SpringyEffect::ApplyMatrix(pMatrix, SpringyEffect::GetParams(pLoco->Owner, true));

	if (pKey)
		*pKey = VoxelIndexKey(-1);

	return 0;
}

// VXL seam fill. Widening scales the draw matrix non-uniformly, which makes the
// voxel rasterizer sample screen columns sparsely and leave 1px vertical gaps.
// Those gaps are only repairable in the sprite the drawer packs, so while that
// sprite is built we fill interior transparent pixels of every raw row right
// before it is RLE-compressed.

// FUN_006c89e0 (0x6C89E0) is the voxel sprite builder, called from DrawVoxel at
// 0x706A11 and 0x706AFD.
DEFINE_HOOK(0x6C89E0, VoxelSpriteBuild_SpringySeamArm, 0x5)
{
	GET_STACK(uintptr_t, returnAddress, 0x0);

	++Springy::Profiler::SpriteBuilds;

	// Only sprites rasterized for a springing object while the effect is on need
	// the seam fill; everything else keeps vanilla output (and cost).
	const bool springing = SpringyEffect::Active
		&& SpringyVoxel::CurrentVoxelSpringing
		&& (returnAddress == 0x706A16 || returnAddress == 0x706B02);

	SpringyVoxel::DilateRows = springing;

	if (springing)
		++Springy::Profiler::SpriteBuildsSpringing;

	return 0;
}

// FUN_006c8b10 (0x6C8B10) is a sibling sprite builder; do not let it inherit the
// armed flag.
DEFINE_HOOK(0x6C8B10, VoxelSpriteBuild_SpringySeamDisarm, 0x5)
{
	SpringyVoxel::DilateRows = false;
	return 0;
}

// FUN_00661530 (0x661530, __cdecl): RLE-compress one raw surface row.
//   [ESP+4] = raw pixel row (palette indices, 0 = transparent)
//   [ESP+8] = destination
//   [ESP+0xC] = row width
DEFINE_HOOK(0x661530, VoxelRow_SpringySeamFill, 0x5)
{
	++Springy::Profiler::RleRows;

	if (!SpringyVoxel::DilateRows)
		return 0;

	GET_STACK(byte*, pRow, 0x4);
	GET_STACK(int, width, 0xC);

	if (pRow && width > 2)
	{
		for (int x = 1; x < width - 1; ++x)
		{
			if (pRow[x] == 0 && pRow[x - 1] != 0 && pRow[x + 1] != 0)
				pRow[x] = pRow[x - 1];
		}
	}

	return 0;
}
