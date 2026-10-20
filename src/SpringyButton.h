#pragma once
#include <GadgetClass.h>

// A small standalone button drawn on top of the vanilla AdvancedCommandBar
// (bottom command bar). It is not part of that bar's own (hardcoded) dispatch;
// it is an independent gadget that happens to be positioned over the bar.
class SpringyButtonClass : public GadgetClass
{
public:
	SpringyButtonClass(int x, int y, int width, int height);
	~SpringyButtonClass() = default;

	virtual bool Draw(bool forced) override;
	virtual void OnMouseEnter() override;
	virtual void OnMouseLeave() override;
	virtual bool Action(GadgetFlag flags, DWORD* pKey, KeyModifier modifier) override;

	void UpdatePosition();

	static void InitIO();
	static void InitClear();

	static SpringyButtonClass* Instance;

public:
	bool IsHovering { false };
};
