#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace night_movers
{
	template <class T>
	T Read(const void* entity, std::size_t offset)
	{
		T value;
		std::memcpy(&value, static_cast<const std::byte*>(entity) + offset, sizeof(value));
		return value;
	}

	template <class T>
	void Write(void* entity, std::size_t offset, const T& value)
	{
		std::memcpy(static_cast<std::byte*>(entity) + offset, &value, sizeof(value));
	}

	using Vec3 = std::array<float, 3>;
	using Quat = std::array<float, 4>;

	class Registry
	{
		std::array<std::uint32_t, 64> words_{};

	public:
		bool Empty() const
		{
			for (auto word : words_)
				if (word) return false;
			return true;
		}

		void Clear()
		{
			words_.fill(0);
		}

		void Mark(unsigned id, bool inUse)
		{
			if (id < 2048 && inUse) words_[id >> 5] |= 0x80000000u >> (id & 31);
		}

		void Remove(unsigned id)
		{
			if (id < 2048) words_[id >> 5] &= ~(0x80000000u >> (id & 31));
		}

		bool Contains(unsigned id, bool inUse) const
		{
			return id < 2048 && inUse && (words_[id >> 5] & (0x80000000u >> (id & 31)));
		}
	};

	template <class Services>
	void CalculateDeltas(void* entity, const Vec3& inputMove, const Vec3& inputTurn, Vec3& move, Vec3& turn, Services& api)
	{
		if (Read<std::uintptr_t>(entity, 0x2C0))
		{
			move = inputMove;
			turn = inputTurn;
			return;
		}
		if (Read<std::uint16_t>(entity, 8) == 6 && (Read<unsigned>(entity, 0x60) & 0x200) && Read<int>(entity, 0x10) == 12 &&
			Read<int>(entity, 0x34) == 12)
		{
			move = Read<Vec3>(entity, 0x2A8);
			turn = Read<Vec3>(entity, 0x2B4);
			return;
		}
		api.Evaluate(entity, move, turn);
		const auto origin = Read<Vec3>(entity, 0x130), angles = Read<Vec3>(entity, 0x13C);
		for (unsigned i = 0; i < 3; ++i)
		{
			move[i] -= origin[i];
			turn[i] -= angles[i];
		}
	}

	// Original 1.28 1C916E0: calculate a future transform without advancing the
	// server entity, then keyframe the main physics world and warp its detail copy.
	// Services keep trajectory evaluation, physics ownership and linking native.
	template <class Services>
	bool UpdateEntity(void* entity, Services& api)
	{
		int mainId{}, detailId{};
		if (!api.Instances(entity, mainId, detailId)) return false;
		// Preserve the actual source branch at 1C917E3..1C917F2, including the
		// detail-only exclusion. Do not infer this condition from its name.
		if (mainId == -1 && detailId != -1) return false;
		std::array<std::byte, 0x5A0> saved{};
		std::memcpy(saved.data(), entity, saved.size());
		const auto oldOrigin = Read<Vec3>(entity, 0x130), oldAngles = Read<Vec3>(entity, 0x13C);
		auto origin = oldOrigin, angles = oldAngles;
		Vec3 deltaOrigin{}, deltaAngles{};
		bool calculate = true;
		if (Read<std::uintptr_t>(entity, 0x2C0))
		{
			const int instance = api.Instance(entity);
			calculate = instance != -1 && (api.Contents(instance) & 0x2014011u) != 0;
			if (calculate)
			{
				api.LinkedTransform(entity);
				const auto linkedOrigin = Read<Vec3>(entity, 0x130), linkedAngles = Read<Vec3>(entity, 0x13C);
				for (unsigned i = 0; i < 3; ++i)
				{
					deltaOrigin[i] = linkedOrigin[i] - oldOrigin[i];
					deltaAngles[i] = linkedAngles[i] - oldAngles[i];
				}
				Write(entity, 0x130, oldOrigin);
				Write(entity, 0x13C, oldAngles);
				api.Link(entity);
			}
		}
		else
			calculate = Read<int>(entity, 0x10) != 0x16;
		if (calculate)
		{
			Vec3 move{}, turn{};
			api.Deltas(entity, deltaOrigin, deltaAngles, move, turn);
			const auto currentOrigin = Read<Vec3>(entity, 0x130), currentAngles = Read<Vec3>(entity, 0x13C);
			for (unsigned i = 0; i < 3; ++i)
			{
				origin[i] = currentOrigin[i] + move[i];
				angles[i] = currentAngles[i] + turn[i];
			}
		}
		std::memcpy(entity, saved.data(), saved.size());
		Quat orientation{ 0, 0, 0, 1 };
		const auto kind = Read<std::uint8_t>(entity, 0xF9);
		if (kind != 1 && kind != 3) api.AnglesToQuat(angles, orientation);
		if (mainId != -1) api.Keyframe(mainId, origin, orientation);
		if (detailId != -1) api.WarpDetail(detailId, origin, orientation);
		return true;
	}
}
