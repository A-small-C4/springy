#include <windows.h>

#include "Profiler.h"
#include "SpringyEffect.h"
#include "Config.h"

#include <cstdio>
#include <cstring>

namespace Springy::Profiler
{
	bool Enabled = false;

	long long MainLoopTicks = 0;
	long long VoxelHookTicks = 0;
	long long SHPHookTicks = 0;
	long long AnimHookTicks = 0;

	long long VoxelDraws = 0;
	long long VoxelDrawsSpringing = 0;
	long long SpriteBuilds = 0;
	long long SpriteBuildsSpringing = 0;
	long long RleRows = 0;

	long long SHPQueries = 0;
	long long SHPBuilds = 0;
	long long SHPBuildTicks = 0;
	long long SHPBuildBytes = 0;

	long long DirtyAdds = 0;
	long long AnimDraws = 0;

	namespace
	{
		long long Freq = 0;
		long long StartTick = 0;
		long long LastFlush = 0;
		long long IntervalFrames = 0;

		char FilePath[MAX_PATH] = "";

		void BuildPath()
		{
			GetModuleFileNameA(nullptr, FilePath, MAX_PATH);
			char* slash = std::strrchr(FilePath, '\\');

			if (slash)
				*(slash + 1) = 0;

			strncat_s(FilePath, MAX_PATH, "springy_profile.txt", _TRUNCATE);
		}

		void Write(const char* line)
		{
			if (!FilePath[0])
				BuildPath();

			FILE* f = nullptr;

			if (fopen_s(&f, FilePath, "a") == 0 && f)
			{
				fputs(line, f);
				fclose(f);
			}
		}

		void Reset()
		{
			MainLoopTicks = VoxelHookTicks = SHPHookTicks = AnimHookTicks = 0;
			VoxelDraws = VoxelDrawsSpringing = 0;
			SpriteBuilds = SpriteBuildsSpringing = RleRows = 0;
			SHPQueries = SHPBuilds = SHPBuildTicks = SHPBuildBytes = 0;
			DirtyAdds = AnimDraws = 0;
			IntervalFrames = 0;
		}

		void Flush(long long now)
		{
			const double dt = static_cast<double>(now - LastFlush) / static_cast<double>(Freq);
			const long long frames = IntervalFrames;

			LastFlush = now;

			if (frames <= 0)
			{
				Reset();
				return;
			}

			const double f = static_cast<double>(frames);
			const double us = 1000000.0 / static_cast<double>(Freq);

			char line[512];
			sprintf_s(line,
				"t=%.1f fps=%.1f frame_ms=%.2f active=%d"
				" mainloop_us=%.1f vox_us=%.1f shp_us=%.1f anim_us=%.1f"
				" draws=%.1f drawsB=%.1f builds=%.1f buildsB=%.1f rows=%.0f"
				" shpQ=%.1f dirty=%.1f animD=%.0f"
				" shpB=%lld shpB_ms=%.2f shpB_KB=%.0f\n",
				static_cast<double>(now - StartTick) / static_cast<double>(Freq),
				f / dt,
				dt * 1000.0 / f,
				SpringyEffect::Active ? 1 : 0,
				static_cast<double>(MainLoopTicks) * us / f,
				static_cast<double>(VoxelHookTicks) * us / f,
				static_cast<double>(SHPHookTicks) * us / f,
				static_cast<double>(AnimHookTicks) * us / f,
				static_cast<double>(VoxelDraws) / f,
				static_cast<double>(VoxelDrawsSpringing) / f,
				static_cast<double>(SpriteBuilds) / f,
				static_cast<double>(SpriteBuildsSpringing) / f,
				static_cast<double>(RleRows) / f,
				static_cast<double>(SHPQueries) / f,
				static_cast<double>(DirtyAdds) / f,
				static_cast<double>(AnimDraws) / f,
				SHPBuilds,
				static_cast<double>(SHPBuildTicks) * 1000.0 / static_cast<double>(Freq),
				static_cast<double>(SHPBuildBytes) / 1024.0);

			Write(line);
			Reset();
		}
	}

	void SetEnabled(bool on)
	{
		if (on == Enabled)
			return;

		Enabled = on;

		if (!on)
			return;

		LARGE_INTEGER f = {};
		QueryPerformanceFrequency(&f);
		Freq = f.QuadPart;

		LARGE_INTEGER t = {};
		QueryPerformanceCounter(&t);
		StartTick = LastFlush = t.QuadPart;

		Reset();

		char line[256];
		sprintf_s(line,
			"# springy profile start: build=%s Rate=%d Amount=%d Widen=%d Narrow=%d Stretch=%d SHP=%d Shadows=%d OnlyOwner=%d\n",
#ifdef _DEBUG
			"Debug",
#else
			"Release",
#endif
			Springy::Rate, Springy::Amount, Springy::Widen, Springy::Narrow, Springy::Stretch,
			Springy::SHP ? 1 : 0, Springy::Shadows ? 1 : 0, Springy::OnlyOwner ? 1 : 0);
		Write(line);
	}

	bool IsEnabled()
	{
		return Enabled;
	}

	long long Now()
	{
		if (!Enabled)
			return 0;

		LARGE_INTEGER t = {};
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}

	void Frame()
	{
		if (!Enabled)
			return;

		++IntervalFrames;

		LARGE_INTEGER t = {};
		QueryPerformanceCounter(&t);

		if (t.QuadPart - LastFlush < Freq * 3)
			return;

		Flush(t.QuadPart);
	}

	void Mark(const char* what)
	{
		if (!Enabled)
			return;

		LARGE_INTEGER t = {};
		QueryPerformanceCounter(&t);
		Flush(t.QuadPart);

		char line[128];
		sprintf_s(line, "# %s\n", what);
		Write(line);
	}
}
