// The shared stream rows' parking rule (master: Tools\Shared Plugin Code, every copy kept equal by shared code sync.py)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.
//
// A projectile whose row of lights could not hang waits here while it flies. Held back by the light cap, it is tried again each frame:
// the cap is full only while other rows are lit, and those keep the pass awake anyway. Held back by its switch, it is tried again only
// when the host's switches move (Wake), so a row switched off never keeps a per-frame pass awake. Game-free: RowParkChecks.h runs it.
#pragma once

#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace RowPark
{
	enum class Why
	{
		kCap,      // the light cap is full, or its lights could not be made
		kHeldBack  // its row is switched off
	};

	template <class T, std::size_t Most = 64>
	class Parked
	{
	public:
		// a_gone(item) is true once its projectile has ended: such items are let go before another is parked
		template <class Gone>
		void Park(T a_item, Why a_why, Gone a_gone)
		{
			Prune(a_gone);
			if (!a_item || Size() >= Most) {
				return;
			}
			(a_why == Why::kCap ? _cap : _held).push_back(std::move(a_item));
		}

		// this frame's retries: only the rows the cap held back (a row that still cannot hang is parked again by its caller)
		[[nodiscard]] std::vector<T> TakeRetries() { return std::exchange(_cap, {}); }

		// the switches moved: every row its switch held back is tried once more, the ended ones let go first
		template <class Gone>
		void Wake(Gone a_gone)
		{
			Prune(a_gone);
			_cap.insert(_cap.end(), std::make_move_iterator(_held.begin()), std::make_move_iterator(_held.end()));
			_held.clear();
		}

		template <class Gone>
		void Prune(Gone a_gone)
		{
			std::erase_if(_cap, a_gone);
			std::erase_if(_held, a_gone);
		}

		// at a game load: everything, for the caller to let go
		[[nodiscard]] std::vector<T> TakeAll()
		{
			auto all = std::exchange(_cap, {});
			all.insert(all.end(), std::make_move_iterator(_held.begin()), std::make_move_iterator(_held.end()));
			_held.clear();
			return all;
		}

		[[nodiscard]] bool        KeepsAwake() const noexcept { return !_cap.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return _cap.size() + _held.size(); }

	private:
		std::vector<T> _cap;
		std::vector<T> _held;
	};
}
