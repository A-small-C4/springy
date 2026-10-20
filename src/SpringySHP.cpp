#include "SpringyEffect.h"
#include "Config.h"
#include "Profiler.h"

#include <TechnoClass.h>
#include <FootClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <AircraftClass.h>
#include <AnimClass.h>
#include <TacticalClass.h>
#include <FileFormats/SHP.h>
#include <Fundamentals.h>
#include <YRMath.h>
#include <Helpers/Macro.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <vector>

// SHP squash: the game blits SHP frames with no scaling, so the "spring" is
// baked into scaled, uncompressed copies of the SHP frames and the draw is
// redirected to those copies (palette/remap/z stay correct).
//
//  - Techno bodies (Infantry/Building/...): substituted at the shared SHP draw
//    helper TechnoClass::DrawObject (0x705E00), bottom-anchored.
//  - Building overlay anims (active/idle anims, damage fire): AnimClass draws
//    through AnimClass::DrawIt (0x422CA0), so the SHP is substituted at its
//    four blit call sites. These anims share the building's canvas, so they are
//    squashed about the building body frame's bottom-centre to stay attached.
//
// A static building is otherwise never re-rendered, so the main loop keeps
// springing buildings marked for redraw and their screen rects dirty.

namespace SpringySHP
{
	constexpr int Steps = SpringyEffect::Steps;

	// Set while AnimClass::DrawIt is running so the shared shape blitter knows to
	// swap in the squashed SHP for the current overlay.
	bool CurrentAnimSquash = false;

	// Building overlay anims are usually a sub-part of the building, so they must
	// squash about the building's base rather than their own frame bottom. The
	// pivot is the building body frame's bottom-centre, in SHP frame coordinates.
	bool CurrentAnimUsePivot = false;
	Point2D CurrentAnimPivot = { 0, 0 };

	// Canvas dimensions of the body SHP the current overlay belongs to. Damage
	// fire and other reused generic art use a different canvas from the building
	// body, so the building-base pivot is meaningless for them and they fall back
	// to squashing about their own frame bottom.
	short CurrentAnimOwnerCanvasW = 0;
	short CurrentAnimOwnerCanvasH = 0;

	// Pivot plus the owning building's canvas size and deformation parameters, for
	// overlays matched through a building's anim lists (these have no OwnerObject).
	struct AnimPivotInfo
	{
		Point2D Pivot { 0, 0 };
		short CanvasW = 0;
		short CanvasH = 0;
		SpringyEffect::Params Params {};
	};

	std::unordered_map<AnimClass*, AnimPivotInfo> AnimPivot;

	// Deformation parameters of the overlay currently being drawn.
	SpringyEffect::Params CurrentAnimParams {};

	// Overlay anims (building active/idle anims, damage fire, ...) are plain map
	// anims with no OwnerObject, so they are matched by the anim lists of the
	// springing buildings instead.
	std::unordered_set<AnimClass*> SpringingAnims;

	// Per-building incremental bookkeeping. Rebuilding the shared maps from
	// scratch every frame was the single biggest cost of the effect in game, so
	// state is kept across frames and only touched when something actually
	// changes: the anim slot lists, the body frame, or an overlay anim frame. A
	// per-frame sweep drops entries of destroyed buildings.
	struct BuildingSpringyState
	{
		AnimClass* Anims[0x15] = {};
		AnimClass* FireAnims[0x8] = {};
		int AnimFrames[0x15 + 0x8] = {};
		AnimPivotInfo Info;
		int LastStep = -1;
		int LastBodyFrame = -1;
		int LastSeen = -1;
	};

	std::unordered_map<BuildingClass*, BuildingSpringyState> BuildingStates;
	int LastEffectGeneration = -1;
	int SweepTick = 0;

	// Non-building SHP draws only need a forced repaint when the phase step
	// changes (the substituted sprite is picked by the step). Direct-mapped
	// last-step cache keyed by object pointer; a collision only causes a
	// redundant dirty add, never a missed one for a pixel change.
	constexpr int DirtyStepCacheSize = 1024;

	struct DirtyStepSlot
	{
		const void* Ptr = nullptr;
		int Step = -1;
	};

	DirtyStepSlot DirtyStepCache[DirtyStepCacheSize];

	void ResetDirtySteps()
	{
		for (auto& slot : DirtyStepCache)
		{
			slot.Ptr = nullptr;
			slot.Step = -1;
		}
	}

	void GetFactors(int step, const SpringyEffect::Params& params, float& wx, float& wy)
	{
		const double pulse = SpringyEffect::WavePulse(step);

		// The tall/thin half of the wave reads Narrow/Stretch, the wide/short
		// half reads Widen/Amount.
		const double horizAmount =
			std::clamp(pulse >= 0.0 ? params.Widen : params.Narrow, 0, 90) / 100.0;
		const double vertAmount =
			std::clamp(pulse >= 0.0 ? params.Amount : params.Stretch, 0, 90) / 100.0;

		wx = static_cast<float>(1.0 + horizAmount * pulse);
		wy = static_cast<float>(1.0 - vertAmount * pulse);
	}

	// Phase bucket (0..Steps-1) over one full wave; also part of the scaled SHP
	// cache key.
	int CurrentStep(int rate)
	{
		rate = std::max(rate, 1);

		const int bucket = static_cast<int>(
			static_cast<long long>(Unsorted::CurrentFrame % rate) * Steps / rate);

		return std::min(bucket, Steps - 1);
	}

	// Decode a frame into a raw Width*Height palette-index buffer (0 = transparent).
	bool DecodeFrame(SHPStruct* pSHP, int frame, std::vector<byte>& out, int& w, int& h, short& left, short& top)
	{
		const auto pFile = pSHP->GetData();

		if (!pFile || frame < 0 || frame >= pFile->Frames)
			return false;

		const auto& fh = pFile->GetFrameHeader(frame);
		w = fh.Width;
		h = fh.Height;
		left = fh.Left;
		top = fh.Top;

		if (w <= 0 || h <= 0 || w > 2048 || h > 2048)
			return false;

		const byte* src = pSHP->GetPixels(frame);

		if (!src)
			return false;

		out.assign(static_cast<size_t>(w) * h, 0);

		if (pSHP->HasCompression(frame))
		{
			// Each row is prefixed by a 2-byte length (total row size); the RLE
			// payload is: a non-zero byte is one opaque pixel, a zero byte is
			// followed by the number of transparent pixels to skip. Each row
			// consumes exactly Width pixels.
			const byte* p = src;
			byte* dst = out.data();

			for (int y = 0; y < h; ++y)
			{
				const byte* rowStart = p;
				const unsigned short rowLen = *reinterpret_cast<const unsigned short*>(p);
				p += 2;

				int x = 0;

				while (x < w)
				{
					const byte c = *p++;

					if (c != 0)
					{
						dst[x++] = c;
					}
					else
					{
						const byte skip = *p++;
						x += skip;
					}
				}

				p = rowStart + rowLen;
				dst += w;
			}
		}
		else
		{
			std::memcpy(out.data(), src, static_cast<size_t>(w) * h);
		}

		return true;
	}

	struct FramePixels
	{
		int W = 0, H = 0;
		short Left = 0, Top = 0;
		std::vector<byte> Pixels;
	};

	// Build a squashed SHPStruct (a plain in-memory SHPFile) for one step.
	SHPStruct* BuildSquashed(SHPStruct* pOriginal, int step, const SpringyEffect::Params& params, int pivotX, int pivotY, bool usePivot)
	{
		if (!pOriginal)
			return nullptr;

		const long long pT0 = Springy::Profiler::Now();

		const auto pFile = pOriginal->GetData();

		if (!pFile)
			return nullptr;

		const int frames = pFile->Frames;

		if (frames <= 0 || frames > 4096)
			return nullptr;

		float wx, wy;
		GetFactors(step, params, wx, wy);

		std::vector<FramePixels> scaled(frames);

		for (int f = 0; f < frames; ++f)
		{
			std::vector<byte> px;
			int w = 0, h = 0;
			short left = 0, top = 0;

			if (!DecodeFrame(pOriginal, f, px, w, h, left, top))
				continue;

			const int w2 = std::max(1, static_cast<int>(std::lround(w * wx)));
			const int h2 = std::max(1, static_cast<int>(std::lround(h * wy)));
			std::vector<byte> sp(static_cast<size_t>(w2) * h2, 0);

			for (int y = 0; y < h2; ++y)
			{
				const int sy = std::min(h - 1, y * h / h2);

				for (int x = 0; x < w2; ++x)
				{
					const int sx = std::min(w - 1, x * w / w2);
					sp[static_cast<size_t>(y) * w2 + x] = px[static_cast<size_t>(sy) * w + sx];
				}
			}

			auto& fp = scaled[f];
			fp.W = w2;
			fp.H = h2;

			if (usePivot)
			{
				// Scale about the building's base (given in the anim's local
				// screen coords) so a sub-part stays glued to the body.
				fp.Left = static_cast<short>(std::lround(pivotX + (left - pivotX) * wx));
				fp.Top = static_cast<short>(std::lround(pivotY + (top - pivotY) * wy));
			}
			else
			{
				fp.Left = static_cast<short>(left + (w - w2) / 2);
				fp.Top = static_cast<short>(top + (h - h2)); // anchor bottom
			}

			fp.Pixels = std::move(sp);
		}

		const size_t headerSize = 8 + static_cast<size_t>(frames) * 0x18;
		size_t total = headerSize;

		for (const auto& fp : scaled)
			total += fp.Pixels.size();

		if (total > (64u << 20))
			return nullptr;

		auto* pBuf = new byte[total];

		*reinterpret_cast<short*>(pBuf + 0) = 0; // Type: file (not a reference)
		*reinterpret_cast<short*>(pBuf + 2) = pFile->Width;
		*reinterpret_cast<short*>(pBuf + 4) = pFile->Height;
		*reinterpret_cast<short*>(pBuf + 6) = static_cast<short>(frames);

		int pixelOffset = static_cast<int>(headerSize);

		for (int f = 0; f < frames; ++f)
		{
			const auto& fp = scaled[f];
			byte* fh = pBuf + 8 + static_cast<size_t>(f) * 0x18;

			*reinterpret_cast<short*>(fh + 0x00) = fp.Left;
			*reinterpret_cast<short*>(fh + 0x02) = fp.Top;
			*reinterpret_cast<short*>(fh + 0x04) = static_cast<short>(fp.W);
			*reinterpret_cast<short*>(fh + 0x06) = static_cast<short>(fp.H);
			*reinterpret_cast<int*>(fh + 0x08) = 0; // Flags: uncompressed
			*reinterpret_cast<int*>(fh + 0x0C) = 0; // Color
			*reinterpret_cast<int*>(fh + 0x10) = 0;
			*reinterpret_cast<int*>(fh + 0x14) = pixelOffset;

			if (!fp.Pixels.empty())
				std::memcpy(pBuf + pixelOffset, fp.Pixels.data(), fp.Pixels.size());

			pixelOffset += static_cast<int>(fp.Pixels.size());
		}

		Springy::Profiler::AddElapsed(Springy::Profiler::SHPBuildTicks, pT0);
		++Springy::Profiler::SHPBuilds;
		Springy::Profiler::SHPBuildBytes += static_cast<long long>(total);

		return reinterpret_cast<SHPStruct*>(pBuf);
	}

	std::map<std::tuple<SHPStruct*, int, int, int, int, int, int, int, bool>, SHPStruct*> Cache;

	SHPStruct* GetSquashed(SHPStruct* pOriginal, int step, const SpringyEffect::Params& params, int pivotX, int pivotY, bool usePivot)
	{
		if (!pOriginal)
			return nullptr;

		++Springy::Profiler::SHPQueries;

		// At rest (pulse 0) the deformed copy would be pixel-identical to the
		// original, so skip the copy entirely: this drops two of the eight step
		// variants per SHP, with their build time and memory.
		{
			float wx, wy;
			GetFactors(step, params, wx, wy);

			if (wx == 1.0f && wy == 1.0f)
				return pOriginal;
		}

		const auto key = std::make_tuple(pOriginal, step, params.Amount, params.Widen, params.Narrow, params.Stretch, pivotX, pivotY, usePivot);
		const auto it = Cache.find(key);

		if (it != Cache.end())
			return it->second;

		SHPStruct* pSquashed = BuildSquashed(pOriginal, step, params, pivotX, pivotY, usePivot);
		Cache.emplace(key, pSquashed);
		return pSquashed;
	}

	// Resolve an SHP's canvas size (the declared full-image dimensions shared by
	// all frames), following references to their loaded file.
	bool GetCanvasDims(SHPStruct* pSHP, short& w, short& h)
	{
		if (!pSHP)
			return false;

		if (pSHP->Width > 0 && pSHP->Height > 0)
		{
			w = pSHP->Width;
			h = pSHP->Height;
			return true;
		}

		if (pSHP->IsReference())
		{
			if (const auto pFile = pSHP->GetData())
			{
				w = pFile->Width;
				h = pFile->Height;
				return w > 0 && h > 0;
			}
		}

		return false;
	}

	// Pivot and canvas size for squashing a building overlay: the building body
	// frame's bottom-centre, in SHP frame coordinates. Building overlay anims share
	// the same canvas/origin as the body, so this is directly comparable to the
	// anim's own frame Left/Top.
	AnimPivotInfo ComputeAnimInfo(TechnoClass* pOwner)
	{
		AnimPivotInfo info;
		info.Params = SpringyEffect::GetParams(pOwner);

		const auto pBuilding = reinterpret_cast<BuildingClass*>(pOwner);

		SHPStruct* pImg = pBuilding->Type ? pBuilding->Type->GetImage() : nullptr;

		if (!pImg)
			return info;

		GetCanvasDims(pImg, info.CanvasW, info.CanvasH);

		const int frame = pBuilding->GetCurrentFrame();

		if (frame < 0 || frame >= pImg->Frames)
			return info;

		RectangleStruct bounds = {};
		pImg->GetFrameBounds(bounds, frame);

		info.Pivot = Point2D{ bounds.X + bounds.Width / 2, bounds.Y + bounds.Height };
		return info;
	}

	void DetachBuildingAnims(const BuildingSpringyState& st)
	{
		for (AnimClass* pAnim : st.Anims)
		{
			if (pAnim)
			{
				SpringingAnims.erase(pAnim);
				AnimPivot.erase(pAnim);
			}
		}

		for (AnimClass* pAnim : st.FireAnims)
		{
			if (pAnim)
			{
				SpringingAnims.erase(pAnim);
				AnimPivot.erase(pAnim);
			}
		}
	}

	void AttachBuildingAnims(const BuildingSpringyState& st)
	{
		for (AnimClass* pAnim : st.Anims)
		{
			if (pAnim)
			{
				SpringingAnims.insert(pAnim);
				AnimPivot[pAnim] = st.Info;
			}
		}

		for (AnimClass* pAnim : st.FireAnims)
		{
			if (pAnim)
			{
				SpringingAnims.insert(pAnim);
				AnimPivot[pAnim] = st.Info;
			}
		}
	}

	// Bring one building's cached state up to date; returns true when anything
	// the building draws (phase step, body frame, overlay anim frames, or the
	// anim lists themselves) changed since the previous frame, i.e. when it
	// must be re-rendered and its screen rect re-blitted.
	bool RefreshBuildingState(BuildingClass* pBuilding, BuildingSpringyState& st)
	{
		bool changed = false;

		bool membership = false;

		for (int i = 0; i < 0x15; ++i)
		{
			if (st.Anims[i] != pBuilding->Anims[i])
			{
				membership = true;
				break;
			}
		}

		if (!membership)
		{
			for (int i = 0; i < 0x8; ++i)
			{
				if (st.FireAnims[i] != pBuilding->DamageFireAnims[i])
				{
					membership = true;
					break;
				}
			}
		}

		if (membership)
		{
			DetachBuildingAnims(st);
			std::memcpy(st.Anims, pBuilding->Anims, sizeof(st.Anims));
			std::memcpy(st.FireAnims, pBuilding->DamageFireAnims, sizeof(st.FireAnims));
		}

		const int bodyFrame = pBuilding->GetCurrentFrame();

		if (membership || bodyFrame != st.LastBodyFrame)
		{
			st.LastBodyFrame = bodyFrame;

			// Params and the pivot (the body frame's bottom centre) can both
			// change with the body frame.
			st.Info = ComputeAnimInfo(pBuilding);

			if (membership)
			{
				AttachBuildingAnims(st);
			}
			else
			{
				for (AnimClass* pAnim : st.Anims)
				{
					if (pAnim)
						AnimPivot[pAnim] = st.Info;
				}

				for (AnimClass* pAnim : st.FireAnims)
				{
					if (pAnim)
						AnimPivot[pAnim] = st.Info;
				}
			}

			changed = true;
		}

		// Overlay anims advance their own frames independently of the body.
		for (int i = 0; i < 0x15; ++i)
		{
			if (AnimClass* pAnim = st.Anims[i])
			{
				const int frame = pAnim->Animation.Value;

				if (frame != st.AnimFrames[i])
				{
					st.AnimFrames[i] = frame;
					changed = true;
				}
			}
		}

		for (int i = 0; i < 0x8; ++i)
		{
			if (AnimClass* pAnim = st.FireAnims[i])
			{
				const int frame = pAnim->Animation.Value;

				if (frame != st.AnimFrames[0x15 + i])
				{
					st.AnimFrames[0x15 + i] = frame;
					changed = true;
				}
			}
		}

		const int step = CurrentStep(st.Info.Params.Rate);

		if (step != st.LastStep)
		{
			st.LastStep = step;
			changed = true;
		}

		return changed;
	}

	// The spring pushes pixels outside the object's nominal screen rect: widen
	// grows the footprint symmetrically, and stretch grows it upward from the
	// ground anchor, so the dirty rect must be inflated by the worst-case
	// amplitudes (plus a small slop) or stale pixels from the wide/tall phases
	// are left behind.
	RectangleStruct InflateDirtyRect(const RectangleStruct& dims, const SpringyEffect::Params& params)
	{
		const int w = std::max(dims.Width, 1);
		const int h = std::max(dims.Height, 1);

		// Half of Widen% per side: the widen phase scales the footprint about
		// its centre. Vertical: Stretch% of the height grows upward from the
		// ground anchor; the widen term leaks into the vertical axis under the
		// isometric projection, so fold it in there too.
		const int mx = w * std::clamp(params.Widen, 0, 90) / 200 + 8;
		const int myTop = h * std::clamp(params.Stretch, 0, 90) / 100 + mx + 8;
		const int myBottom = mx;

		return RectangleStruct {
			dims.X - mx,
			dims.Y - myTop,
			w + mx * 2,
			h + myTop + myBottom
		};
	}

	// TacticalClass::AddDirtyArea (0x6D2790, __thiscall): append a screen-space
	// rectangle to the tactical blit list so it is refreshed from the temp
	// surface. The object is passed in ECX.
	void AddDirtyArea(const RectangleStruct& rect)
	{
		++Springy::Profiler::DirtyAdds;

		reinterpret_cast<void(__fastcall*)(void*, void*, RectangleStruct, char)>(0x6D2790)(
			TacticalClass::Instance, nullptr, rect, 0);
	}

	// Switching the effect off used to leave every deformed object frozen on
	// its last squashed frame until something unrelated (a camera move) forced
	// a repaint: nothing marks a static building for redraw once the effect is
	// off. This forces one repaint of everything that was deforming, using the
	// bookkeeping caches, and drops all per-object state. Call while the effect
	// is still active.
	void RestoreAll()
	{
		for (auto& entry : BuildingStates)
		{
			BuildingClass* pBuilding = entry.first;
			const auto& st = entry.second;

			if (pBuilding)
			{
				pBuilding->MarkForRedraw();

				RectangleStruct dims = {};
				pBuilding->GetRenderDimensions(&dims);

				if (dims.Width > 0 && dims.Height > 0)
					AddDirtyArea(InflateDirtyRect(dims, st.Info.Params));
			}

			DetachBuildingAnims(st);
		}

		BuildingStates.clear();
		SpringingAnims.clear();
		AnimPivot.clear();
		ResetDirtySteps();

		// Units and aircraft redraw continuously through their own animation,
		// but forcing one extra repaint restores them immediately (and covers
		// paused or docked ones).
		const auto dirtyTechno = [](TechnoClass* pTechno)
		{
			if (!pTechno || !SpringyEffect::ShouldSpring(pTechno))
				return;

			pTechno->MarkForRedraw();

			RectangleStruct dims = {};
			pTechno->GetRenderDimensions(&dims);

			if (dims.Width > 0 && dims.Height > 0)
				AddDirtyArea(InflateDirtyRect(dims, SpringyEffect::GetParams(pTechno)));
		};

		for (int i = 0; i < InfantryClass::Array.Count; ++i)
		{
			if (InfantryClass* pInfantry = InfantryClass::Array[i])
				dirtyTechno(pInfantry);
		}

		for (int i = 0; i < UnitClass::Array.Count; ++i)
		{
			if (UnitClass* pUnit = UnitClass::Array[i])
				dirtyTechno(pUnit);
		}

		for (int i = 0; i < AircraftClass::Array.Count; ++i)
		{
			if (AircraftClass* pAircraft = AircraftClass::Array[i])
				dirtyTechno(pAircraft);
		}
	}
}

// TechnoClass::DrawObject (0x705E00): this = ECX, SHP = [ESP+4]. Substitute the
// SHP with the squashed copy; the frame index and position stay as they are
// (the scaled frames are bottom-anchored).
DEFINE_HOOK(0x705E00, TechnoClass_DrawObject_SpringyEffect, 0x7)
{
	if (!SpringyEffect::Active || !Springy::SHP)
		return 0;

	GET(TechnoClass*, pThis, ECX);
	GET_STACK(SHPStruct*, pSHP, 0x4);

	if (!pSHP || !pThis)
		return 0;

	// Only real Technos reach this helper; AnimClass draws go through
	// AnimClass::DrawIt (0x422CA0) instead, so guard the type before touching
	// TechnoClass fields.
	const AbstractType what = pThis->WhatAmI();

	if (what != AbstractType::Unit && what != AbstractType::Aircraft
		&& what != AbstractType::Building && what != AbstractType::Infantry)
		return 0;

	if (!SpringyEffect::ShouldSpring(pThis))
		return 0;

	const long long pT0 = Springy::Profiler::Now();

	const auto params = SpringyEffect::GetParams(pThis);

	// A non-building's drawn pixels only change when the phase step does (the
	// substituted sprite is picked by the step), so force a repaint only then;
	// between steps the game would repaint the identical image. Buildings are
	// covered by the main-loop bookkeeping, which dirties them only when their
	// pixels actually change - do not duplicate those rects here.
	if (what != AbstractType::Building)
	{
		const int step = SpringySHP::CurrentStep(params.Rate);
		auto& slot = SpringySHP::DirtyStepCache[
			(reinterpret_cast<uintptr_t>(pThis) >> 4) & (SpringySHP::DirtyStepCacheSize - 1)];

		if (slot.Ptr != pThis || slot.Step != step)
		{
			slot.Ptr = pThis;
			slot.Step = step;

			const CoordStruct coords = pThis->GetRenderCoords();
			const auto client = TacticalClass::Instance->CoordsToClient(coords);

			if (client.second)
			{
				// GetRenderDimensions returns an absolute screen-space rectangle.
				RectangleStruct dims = {};
				pThis->GetRenderDimensions(&dims);
				SpringySHP::AddDirtyArea(SpringySHP::InflateDirtyRect(dims, params));
			}
		}
	}

	if (SHPStruct* pSquashed = SpringySHP::GetSquashed(pSHP, SpringySHP::CurrentStep(params.Rate),
		params, 0, 0, false))
		R->Stack<SHPStruct*>(0x4, pSquashed);

	Springy::Profiler::AddElapsed(Springy::Profiler::SHPHookTicks, pT0);

	return 0;
}

// AnimClass::DrawIt (0x422CA0) entry: decide once whether this overlay belongs
// to a springing Techno. Its SHP is then swapped at the four blit call sites
// below. Only those sites are hooked (not the shared blitter 0x4AED70, which
// every shape draw uses).
DEFINE_HOOK(0x422CA0, AnimClass_Draw_SpringyBegin, 0x5)
{
	++Springy::Profiler::AnimDraws;
	const long long pT0 = Springy::Profiler::Now();

	SpringySHP::CurrentAnimSquash = false;
	SpringySHP::CurrentAnimUsePivot = false;
	SpringySHP::CurrentAnimPivot = Point2D{ 0, 0 };
	SpringySHP::CurrentAnimOwnerCanvasW = 0;
	SpringySHP::CurrentAnimOwnerCanvasH = 0;
	SpringySHP::CurrentAnimParams = SpringyEffect::Params {
		std::max(Springy::Rate, 1),
		std::clamp(Springy::Amount, 0, 90),
		std::clamp(Springy::Widen, 0, 90),
		std::clamp(Springy::Narrow, 0, 90),
		std::clamp(Springy::Stretch, 0, 90)
	};

	if (!SpringyEffect::Active || !Springy::SHP)
	{
		Springy::Profiler::AddElapsed(Springy::Profiler::AnimHookTicks, pT0);
		return 0;
	}

	GET(AnimClass*, pAnim, ECX);

	if (!pAnim)
	{
		Springy::Profiler::AddElapsed(Springy::Profiler::AnimHookTicks, pT0);
		return 0;
	}

	ObjectClass* pOwner = pAnim->OwnerObject;

	if (pOwner)
	{
		const AbstractType ownerWhat = pOwner->WhatAmI();

		if (ownerWhat == AbstractType::Unit || ownerWhat == AbstractType::Aircraft
			|| ownerWhat == AbstractType::Building || ownerWhat == AbstractType::Infantry)
		{
			if (SpringyEffect::ShouldSpring(reinterpret_cast<TechnoClass*>(pOwner)))
			{
				SpringySHP::CurrentAnimSquash = true;

				if (ownerWhat == AbstractType::Building)
				{
					// A building overlay is usually a sub-part: squash about the
					// building's base, not the sub-part's own bottom. An overlay
					// with a mismatched canvas falls back at the blit site.
					const auto info = SpringySHP::ComputeAnimInfo(reinterpret_cast<TechnoClass*>(pOwner));
					SpringySHP::CurrentAnimUsePivot = true;
					SpringySHP::CurrentAnimPivot = info.Pivot;
					SpringySHP::CurrentAnimOwnerCanvasW = info.CanvasW;
					SpringySHP::CurrentAnimOwnerCanvasH = info.CanvasH;
					SpringySHP::CurrentAnimParams = info.Params;
				}
			}
		}
	}
	else if (SpringySHP::SpringingAnims.count(pAnim))
	{
		SpringySHP::CurrentAnimSquash = true;
		SpringySHP::CurrentAnimUsePivot = true;

		const auto it = SpringySHP::AnimPivot.find(pAnim);

		if (it != SpringySHP::AnimPivot.end())
		{
			SpringySHP::CurrentAnimPivot = it->second.Pivot;
			SpringySHP::CurrentAnimOwnerCanvasW = it->second.CanvasW;
			SpringySHP::CurrentAnimOwnerCanvasH = it->second.CanvasH;
			SpringySHP::CurrentAnimParams = it->second.Params;
		}
	}

	Springy::Profiler::AddElapsed(Springy::Profiler::AnimHookTicks, pT0);

	return 0;
}

// The shape argument of the 0x4AED70 blit is the last pushed (i.e. [ESP+0] at
// the call). Replace it with the squashed copy while a springing overlay draws.
DEFINE_HOOK(0x4233E9, AnimClass_Draw_BlitSHP, 0x5)
{
	if (SpringySHP::CurrentAnimSquash)
	{
		REF_STACK(SHPStruct*, pSHP, 0x0);

		if (reinterpret_cast<uintptr_t>(pSHP) >= 0x10000)
		{
			// A reused generic overlay (e.g. damage fire) has a canvas different
			// from the building body's, so the building-base pivot is meaningless
			// for it and it must squash about its own frame bottom instead.
			bool usePivot = SpringySHP::CurrentAnimUsePivot;

			if (usePivot
				&& SpringySHP::CurrentAnimOwnerCanvasW > 0
				&& SpringySHP::CurrentAnimOwnerCanvasH > 0)
			{
				short w = 0, h = 0;

				if (SpringySHP::GetCanvasDims(pSHP, w, h)
					&& (w != SpringySHP::CurrentAnimOwnerCanvasW
						|| h != SpringySHP::CurrentAnimOwnerCanvasH))
				{
					usePivot = false;
				}
			}

			if (SHPStruct* pSquashed = SpringySHP::GetSquashed(pSHP,
				SpringySHP::CurrentStep(SpringySHP::CurrentAnimParams.Rate),
				SpringySHP::CurrentAnimParams,
				SpringySHP::CurrentAnimPivot.X, SpringySHP::CurrentAnimPivot.Y, usePivot))
				pSHP = pSquashed;
		}
	}

	return 0;
}

DEFINE_HOOK_AGAIN(0x4236F7, AnimClass_Draw_BlitSHP, 0x5)
DEFINE_HOOK_AGAIN(0x423827, AnimClass_Draw_BlitSHP, 0x5)
DEFINE_HOOK_AGAIN(0x42389E, AnimClass_Draw_BlitSHP, 0x5)

// The game only re-renders objects it deems dirty, so a static building's
// squashed SHP is never re-drawn (only a camera move forces it). Keep every
// springing building marked for redraw each frame from the main loop, which
// increments Unsorted::CurrentFrame at 0x55DE81.
DEFINE_HOOK(0x55DE81, MainLoop_SpringyEffect_MarkBuildings, 0x6)
{
	Springy::Load();
	Springy::Profiler::Frame();

	if (!SpringyEffect::Active || !Springy::SHP)
		return 0;

	const long long pT0 = Springy::Profiler::Now();

	// Switching the effect on/off invalidates every cached per-object state.
	if (SpringySHP::LastEffectGeneration != SpringyEffect::Generation)
	{
		SpringySHP::LastEffectGeneration = SpringyEffect::Generation;
		SpringySHP::SpringingAnims.clear();
		SpringySHP::AnimPivot.clear();
		SpringySHP::BuildingStates.clear();
		SpringySHP::ResetDirtySteps();
	}

	const int tick = ++SpringySHP::SweepTick;

	for (int i = 0; i < BuildingClass::Array.Count; ++i)
	{
		BuildingClass* pBuilding = BuildingClass::Array[i];

		if (!pBuilding)
			continue;

		auto it = SpringySHP::BuildingStates.find(pBuilding);

		if (!SpringyEffect::ShouldSpring(pBuilding))
		{
			if (it != SpringySHP::BuildingStates.end())
			{
				SpringySHP::DetachBuildingAnims(it->second);
				SpringySHP::BuildingStates.erase(it);
			}

			continue;
		}

		if (it == SpringySHP::BuildingStates.end())
			it = SpringySHP::BuildingStates.emplace(pBuilding, SpringySHP::BuildingSpringyState {}).first;

		auto& st = it->second;
		st.LastSeen = tick;

		// Only force a redraw when something this building draws actually
		// changed; a static building with idle overlays used to be re-rendered
		// and re-blitted every single frame.
		if (SpringySHP::RefreshBuildingState(pBuilding, st))
		{
			pBuilding->MarkForRedraw();

			RectangleStruct dims = {};
			pBuilding->GetRenderDimensions(&dims);

			if (dims.Width > 0 && dims.Height > 0)
				SpringySHP::AddDirtyArea(SpringySHP::InflateDirtyRect(dims, st.Info.Params));
		}
	}

	// Drop buildings that no longer exist so their anim pointers cannot go
	// stale and be matched by a recycled AnimClass address.
	for (auto it = SpringySHP::BuildingStates.begin(); it != SpringySHP::BuildingStates.end();)
	{
		if (it->second.LastSeen != tick)
		{
			SpringySHP::DetachBuildingAnims(it->second);
			it = SpringySHP::BuildingStates.erase(it);
		}
		else
		{
			++it;
		}
	}

	Springy::Profiler::AddElapsed(Springy::Profiler::MainLoopTicks, pT0);

	return 0;
}
