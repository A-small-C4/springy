#pragma once

// In-game performance probe for the spring effect. Enabled with
// [springy] Profile=yes; appends one summary line every few seconds to
// springy_profile.txt next to the game exe, so the cost of the effect can be
// compared on/off and between subsystems. Every entry point is a no-op while
// disabled (Now() returns 0, so a time delta around it collapses to nothing).
namespace Springy::Profiler
{
	void SetEnabled(bool on);
	bool IsEnabled();

	// QPC timestamp in ticks; 0 while disabled.
	long long Now();

	void Frame();                // once per game frame (main loop tick)
	void Mark(const char* what); // flush the interval and annotate an event

	// Raw accumulators, reset at every flush. Contents are meaningless while
	// the profiler has never been enabled.
	extern long long MainLoopTicks;  // our 0x55DE81 building bookkeeping
	extern long long VoxelHookTicks; // our 0x706640 draw hook
	extern long long SHPHookTicks;   // our 0x705E00 draw hook
	extern long long AnimHookTicks;  // our 0x422CA0 draw hook

	extern long long VoxelDraws;          // DrawVoxel calls
	extern long long VoxelDrawsSpringing;  // ... of objects that spring
	extern long long SpriteBuilds;        // voxel sprite builder calls (all)
	extern long long SpriteBuildsSpringing;// ... from a springing draw
	extern long long RleRows;             // voxel sprite rows RLE-compressed

	extern long long SHPQueries;     // GetSquashed cache lookups
	extern long long SHPBuilds;      // squashed SHP variants actually built
	extern long long SHPBuildTicks;  // time spent in BuildSquashed
	extern long long SHPBuildBytes;  // bytes allocated by BuildSquashed

	extern long long DirtyAdds; // AddDirtyArea calls
	extern long long AnimDraws; // AnimClass::DrawIt calls

	inline void AddElapsed(long long& acc, long long t0)
	{
		if (t0)
			acc += Now() - t0;
	}
}
