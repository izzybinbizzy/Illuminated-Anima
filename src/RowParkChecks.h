// The shared stream rows' parking rule (master: Tools\Shared Plugin Code, every copy kept equal by shared code sync.py)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.
//
// The off-game checks of RowPark.h (no game needed), run by each carrying plugin's own tests: call Run with the test's check function
// and add kCount to its floor.
#pragma once

#include "RowPark.h"

#include <array>
#include <cstddef>

namespace RowParkChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 16;

	inline void Run(Check a_check)
	{
		using RowPark::Why;
		std::array<int, 70>  rows{};
		std::array<bool, 70> gone{};
		const auto           ended = [&](const int* a_row) { return gone[static_cast<std::size_t>(a_row - rows.data())]; };

		// a row its switch held back keeps no pass awake, frame after frame
		RowPark::Parked<int*> parked;
		parked.Park(&rows[0], Why::kHeldBack, ended);
		a_check(!parked.KeepsAwake(), "a switched-off row keeps no pass awake", __LINE__);
		int awakeFrames = 0;
		for (int frame = 0; frame < 300; ++frame) {
			if (parked.KeepsAwake()) {
				++awakeFrames;
				for (auto* row : parked.TakeRetries()) {
					parked.Park(row, Why::kHeldBack, ended);
				}
			}
		}
		a_check(awakeFrames == 0, "300 frames with its switch off wake the pass on none", __LINE__);
		a_check(parked.Size() == 1, "the switched-off row stays parked", __LINE__);

		// the switches move: it is tried exactly once, then sleeps again while its switch stays off
		parked.Wake(ended);
		a_check(parked.KeepsAwake(), "a switch change wakes the pass", __LINE__);
		awakeFrames = 0;
		for (int frame = 0; frame < 300; ++frame) {
			if (parked.KeepsAwake()) {
				++awakeFrames;
				for (auto* row : parked.TakeRetries()) {
					parked.Park(row, Why::kHeldBack, ended);
				}
			}
		}
		a_check(awakeFrames == 1, "a switch change costs one frame while the switch stays off", __LINE__);
		a_check(parked.Size() == 1 && !parked.KeepsAwake(), "after that frame it waits again", __LINE__);

		// switched back on: its retry takes it (the caller hangs it), nothing stays parked
		parked.Wake(ended);
		const auto retried = parked.TakeRetries();
		a_check(retried.size() == 1 && retried[0] == &rows[0], "the woken row is handed back for its row to hang", __LINE__);
		a_check(parked.Size() == 0 && !parked.KeepsAwake(), "a hung row leaves the park", __LINE__);

		// a row the light cap held back is tried each frame (other rows are lit and keep the pass awake then anyway)
		parked.Park(&rows[1], Why::kCap, ended);
		a_check(parked.KeepsAwake(), "a capped row is tried again next frame", __LINE__);
		parked.Park(&rows[2], Why::kHeldBack, ended);
		const auto capped = parked.TakeRetries();
		a_check(capped.size() == 1 && capped[0] == &rows[1], "only the capped row is retried, not the switched-off one", __LINE__);

		// an ended projectile is let go: at the next park, and at a wake
		gone[2] = true;
		parked.Wake(ended);
		a_check(parked.Size() == 0, "an ended projectile is let go at a wake", __LINE__);
		parked.Park(&rows[3], Why::kHeldBack, ended);
		parked.Park(&rows[4], Why::kHeldBack, ended);
		a_check(parked.Size() == 2, "rows still flying stay parked", __LINE__);
		gone[3] = true;
		parked.Park(&rows[5], Why::kCap, ended);
		a_check(parked.Size() == 2, "an ended row parked earlier is let go by the next park", __LINE__);

		// nothing null, never more than the most it holds, and a game load takes everything
		parked.Park(nullptr, Why::kCap, ended);
		a_check(parked.Size() == 2, "a null projectile is never parked", __LINE__);
		for (std::size_t i = 6; i < rows.size(); ++i) {
			parked.Park(&rows[i], i % 2 ? Why::kCap : Why::kHeldBack, ended);
		}
		a_check(parked.Size() == 64, "it holds 64 at most", __LINE__);
		const auto all = parked.TakeAll();
		a_check(all.size() == 64 && parked.Size() == 0 && !parked.KeepsAwake(), "a game load takes every parked row", __LINE__);
	}
}
