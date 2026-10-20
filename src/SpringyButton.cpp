#include "SpringyButton.h"

#include "Config.h"
#include "SpringyEffect.h"

#include <GScreenClass.h>
#include <Surface.h>
#include <Drawing.h>
#include <VocClass.h>
#include <RulesClass.h>
#include <MouseClass.h>
#include <Memory.h>
#include <ConvertClass.h>
#include <FileSystem.h>
#include <FileFormats/SHP.h>
#include <HouseClass.h>
#include <HouseTypeClass.h>
#include <Fundamentals.h>
#include <Helpers/Macro.h>

#include <cstring>

SpringyButtonClass* SpringyButtonClass::Instance = nullptr;

namespace
{
	// The vanilla AdvancedCommandBar stores its geometry in a global pointer at
	// 0xB0FC58 pointing to an int[4] { X, Y, Width, Height }.
	int* GetCommandBarRect()
	{
		const auto ppRect = reinterpret_cast<int**>(0xB0FC58);
		const auto pRect = *ppRect;

		if (pRect && reinterpret_cast<uintptr_t>(pRect) > 0x10000 && reinterpret_cast<uintptr_t>(pRect) < 0x80000000)
			return pRect;

		return nullptr;
	}

	bool IsValidPtr(const void* p)
	{
		const auto value = reinterpret_cast<uintptr_t>(p);
		return value > 0x10000 && value < 0x80000000;
	}

	// Command bar object passed to its input handler FUN_006d0680 (0x6D0680).
	// Its byte at +0x5544 is 1 while the bar is expanded (visible) and 0 while it
	// is collapsed (hidden) - the command bar's own show/hide button toggles it.
	// Kept here so the button can hide along with the bar.
	void* pCommandBarOwner = nullptr;

	bool IsCommandBarCollapsed()
	{
		if (!IsValidPtr(pCommandBarOwner))
			return false;

		return *reinterpret_cast<const byte*>(static_cast<const char*>(pCommandBarOwner) + 0x5544) == 0;
	}

	// The command bar buttons are laid out by FUN_006d0fd0 (0x6D0FD0): a slot's
	// command index maps to X = cmdIndex * pitch + pButtonRect->X, where
	// pitch = pButtonRect->Width, and a button is drawn only while its right edge
	// fits inside the clipping rect (pClipRect). Returns the right edge of the
	// rightmost visible button, and its Y in outY. Returns 0 when the bar has not
	// been laid out yet.
	int GetRightmostCommandButtonEdge(int& outY)
	{
		const auto pButtonRect = *reinterpret_cast<int**>(0xB0FC68);
		const auto pClipRect = *reinterpret_cast<int**>(0xB0FC64);

		if (!IsValidPtr(pButtonRect) || !IsValidPtr(pClipRect))
			return 0;

		const int pitch = *reinterpret_cast<int*>(0xB0CC38);

		if (pitch <= 0 || pButtonRect[2] <= 0)
			return 0;

		const int startX = pButtonRect[0];
		const int boundary = pClipRect[0] + pClipRect[2];
		const auto pSlots = reinterpret_cast<const int*>(0xB0CB78);

		int best = -1;
		int rightEdge = 0;

		for (int i = 0; i < 0x19; ++i)
		{
			const int cmd = pSlots[i];

			if (cmd < 0 || cmd >= 0x19)
				continue;

			const int x = cmd * pitch + startX;

			if (x + pitch > boundary) // the game's own visibility test
				continue;

			if (cmd > best)
			{
				best = cmd;
				rightEdge = x + pitch;
			}
		}

		outY = *reinterpret_cast<int*>(0xB0CC24);

		return (best >= 0) ? rightEdge : 0;
	}

	// Icon SHPs are loaded once and cached per side. The filenames come from
	// [springy] Button.SHP.Allied/.Soviet/.Yuri in RA2MD.INI; an empty Yuri
	// entry falls back to the Soviet art.
	enum class IconSide { Allied, Soviet, Yuri };

	IconSide GetIconSide()
	{
		const auto pPlayer = HouseClass::CurrentPlayer;

		if (pPlayer && pPlayer->Type)
		{
			if (pPlayer->Type->SideIndex == 0)
				return IconSide::Allied;

			if (pPlayer->Type->SideIndex == 2 && Springy::ButtonSHPYuri[0])
				return IconSide::Yuri;
		}

		return IconSide::Soviet;
	}

	SHPStruct* LoadIcon(IconSide side)
	{
		static SHPStruct* cache[3] = {};
		static bool loaded[3] = {};
		static char cachedName[3][0x40] = {};

		const int index = static_cast<int>(side);
		const char* name = side == IconSide::Allied
			? Springy::ButtonSHPAllied
			: side == IconSide::Yuri ? Springy::ButtonSHPYuri : Springy::ButtonSHPSoviet;

		if (!loaded[index] || strcmp(cachedName[index], name) != 0)
		{
			cache[index] = FileSystem::LoadSHPFile(name);
			strcpy_s(cachedName[index], name);
			loaded[index] = true;
		}

		return cache[index];
	}
}

// The command bar selects/collapses through its input handler FUN_006d0680
// (0x6D0680), which receives the command bar object in ECX. Capture it so the
// button can read the collapse flag from it.
DEFINE_HOOK(0x6D0680, CommandBar_Input_Capture, 0x5)
{
	GET(void*, pThis, ECX);
	pCommandBarOwner = pThis;
	return 0;
}


SpringyButtonClass::SpringyButtonClass(int x, int y, int width, int height)
	: GadgetClass(x, y, width, height, (GadgetFlag::LeftPress | GadgetFlag::LeftRelease), false)
{
}

void SpringyButtonClass::UpdatePosition()
{
	int x = 0, y = 0;

	int buttonY = 0;
	const int rightEdge = GetRightmostCommandButtonEdge(buttonY);

	if (rightEdge > 0)
	{
		// Directly to the right of the rightmost command bar button.
		x = rightEdge;
		y = buttonY;
	}
	else if (const auto pBar = GetCommandBarRect())
	{
		// Fall back to the right end of the command bar, vertically centred.
		x = pBar[0] + pBar[2] - this->Width;
		y = pBar[1] + (pBar[3] - this->Height) / 2;
	}
	else
	{
		const auto& vb = DSurface::ViewBounds;
		x = vb.X + vb.Width - this->Width;
		y = vb.Y + vb.Height - this->Height;
	}

	this->SetPosition(x + Springy::ButtonOffsetX, y + Springy::ButtonOffsetY);
}

bool SpringyButtonClass::Draw(bool forced)
{
	// Hide together with the command bar (its own show/hide toggle sets the flag
	// this reads).
	const bool hidden = IsCommandBarCollapsed();
	this->Disabled = hidden;

	if (hidden)
		return true;

	// The button follows the rightmost command button, which changes with the
	// selection, so recompute the position every frame.
	this->UpdatePosition();

	const auto pSurface = DSurface::Composite;

	if (!pSurface)
		return true;

	RectangleStruct bounds = pSurface->GetRect();
	const Point2D location { this->X, this->Y };

	if (SHPStruct* pIcon = LoadIcon(GetIconSide()))
	{
		// Frame 0 = inactive, frame 1 = active.
		const int frame = SpringyEffect::Active ? 1 : 0;

		pSurface->DrawSHP(FileSystem::SIDEBAR_PAL, pIcon, frame, &location, &bounds,
			BlitterFlags::bf_400, 0, 0, ZGradient::Ground, 1000, 0, nullptr, 0, 0, 0);
	}

	return true;
}

void SpringyButtonClass::OnMouseEnter()
{
	this->IsHovering = true;
	MouseClass::Instance.UpdateCursor(MouseCursorType::Default, false);
}

void SpringyButtonClass::OnMouseLeave()
{
	this->IsHovering = false;
	MouseClass::Instance.UpdateCursor(MouseCursorType::Default, false);
}

bool SpringyButtonClass::Action(GadgetFlag flags, DWORD* pKey, KeyModifier modifier)
{
	if (flags & GadgetFlag::LeftPress)
	{
		VocClass::PlayGlobal(RulesClass::Instance->GUIBuildSound, 0x2000, 1.0f);
		SpringyEffect::Toggle();
	}

	this->GadgetClass::Action(flags, pKey, KeyModifier::None);
	return true;
}

void SpringyButtonClass::InitIO()
{
	// A new match: start from a clean state so a previous match's stale state
	// (dead HouseClass*, stuck MusicPlaying) does not leak in.
	SpringyEffect::Reset();
	Springy::Load();
	pCommandBarOwner = nullptr;

	if (!Springy::Enabled)
		return;

	// Same size as a native command bar button.
	constexpr int width = 52;
	constexpr int height = 32;

	const auto pButton = GameCreate<SpringyButtonClass>(0, 0, width, height);
	pButton->Zap();
	GScreenClass::Instance.AddButton(pButton);
	Instance = pButton;
	pButton->UpdatePosition();
}

void SpringyButtonClass::InitClear()
{
	if (const auto pButton = Instance)
	{
		Instance = nullptr;
		GScreenClass::Instance.RemoveButton(pButton);
		GameDelete(pButton);
	}
}
