#pragma once

// Strict original IW8 1.64.5 Island SpawnGroup point ownership and source
// disable-state semantics. Never map original type-5 quest points to Replay
// type-3 cache points or silently lose original 20-byte type/flag fields.
// The source blob is an offline-recovered input; this is NOT, by itself, a
// Replay MapEnts hook, ScriptableDef spawn or loot-interaction replacement.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace iw8x::island::original_spawn_group
{
	inline constexpr std::size_t world_points = 2676;
	inline constexpr std::size_t loot_caches = 866;
	inline constexpr std::size_t total_points = 3543;
	inline constexpr std::size_t original_point_bytes = 20;
	inline constexpr std::size_t quest_index = total_points - 1;
	inline constexpr std::uint8_t original_disabled_bit = 1;

	struct point
	{
		std::array<float, 3> origin{};
		std::array<std::int16_t, 3> angles_tenths{};
		std::uint8_t source_type{};
		std::uint8_t source_flags{};
	};
	static_assert(sizeof(point) == original_point_bytes);

	// Keep source serialization apart from the mutable game-session flags.
	// In original 1.64.5 GSC builtin 0x3E3 (RVA 0x34E3880) disable performs
	// an OR into the point's byte offset 0x13. Replay instead overwrites an
	// unrelated 28-byte projected cluster field at +0x18; it cannot replace
	// source-native disable ownership, particularly for point 3542 (type 5).
	class state final
	{
	public:
		bool import(std::span<const std::uint8_t> original_bytes) noexcept
		{
			if (original_bytes.size() != total_points * original_point_bytes) return false;
			std::array<point, total_points> checked{};
			std::size_t world{}, cache{}, quest{};
			for (std::size_t i = 0; i < total_points; ++i)
			{
				point item{};
				std::memcpy(&item, original_bytes.data() + i * original_point_bytes,
					original_point_bytes);
				if (item.source_type == 2) ++world;
				else if (item.source_type == 3) ++cache;
				else if (item.source_type == 5) ++quest;
				else return false;
				for (const auto dimension : item.origin)
					if (!std::isfinite(dimension)) return false;
				checked[i] = item;
			}
			if (world != world_points || cache != loot_caches || quest != 1 ||
				checked[quest_index].source_type != 5)
				return false;
			// Cross-check source index order to avoid accidentally treating an
			// alternate MapEnts or other map's points as original Island data.
			for (std::size_t i = 0; i < world_points; ++i)
				if (checked[i].source_type != 2) return false;
			for (std::size_t i = world_points; i < quest_index; ++i)
				if (checked[i].source_type != 3) return false;
			static constexpr std::array<float, 3> quest_origin{11443.400390625f, -2953.260009765625f, 505.5f};
			if (checked[quest_index].origin != quest_origin) return false;
			original_ = checked;
			reset();
			loaded_ = true;
			return true;
		}

		bool loaded() const noexcept { return loaded_; }
		const point* at(std::size_t index) const noexcept
		{
			return loaded_ && index < total_points ? &original_[index] : nullptr;
		}

		bool disabled(std::size_t index) const noexcept
		{
			return !loaded_ || index >= total_points || (flags_[index] & original_disabled_bit) != 0;
		}

		bool disable(std::size_t index) noexcept
		{
			if (!loaded_ || index >= total_points) return false;
			flags_[index] = static_cast<std::uint8_t>(flags_[index] | original_disabled_bit);
			return true;
		}

		std::uint8_t flags(std::size_t index) const noexcept
		{
			return loaded_ && index < total_points ? flags_[index] : 0;
		}

		void reset() noexcept
		{
			for (std::size_t i = 0; i < total_points; ++i)
				flags_[i] = original_[i].source_flags;
		}

		std::size_t active(std::uint8_t kind) const noexcept
		{
			if (!loaded_) return 0;
			std::size_t result{};
			for (std::size_t i = 0; i < total_points; ++i)
				if (original_[i].source_type == kind && !disabled(i)) ++result;
			return result;
		}

	private:
		std::array<point, total_points> original_{};
		std::array<std::uint8_t, total_points> flags_{};
		bool loaded_{};
	};

	// Native Island source associations are NOT simply 28-byte Replay
	// MapEnts point.cluster/point.zone scalars. The original zones own type-3
	// points directly and type-2 world points via clusters. This separately
	// recovered compact sidecar retains every original point relationship.
	class memberships final
	{
	public:
		bool import(std::span<const std::uint8_t> bytes) noexcept
		{
			static_assert(sizeof(link) == 16); // Native padding: disk is 12 bytes, read individually.
			constexpr std::size_t header = 28;
			constexpr std::size_t disk_link = 12;
			constexpr std::size_t expected_cluster_edges = world_points;
			constexpr std::size_t expected_zone_edges = total_points - 1;
			if (bytes.size() != header + total_points * disk_link +
				(expected_cluster_edges + expected_zone_edges) * sizeof(std::uint16_t)) return false;
			constexpr std::array<char, 8> magic{'I','S','L','M','E','M','0','1'};
			if (std::memcmp(bytes.data(), magic.data(), magic.size())) return false;
			auto u32 = [&](std::size_t at) noexcept
			{
				std::uint32_t result{};
				std::memcpy(&result, bytes.data() + at, sizeof(result));
				return result;
			};
			auto u16 = [&](std::size_t at) noexcept
			{
				std::uint16_t result{};
				std::memcpy(&result, bytes.data() + at, sizeof(result));
				return result;
			};
			if (u32(8) != total_points || u32(12) != 1227 || u32(16) != 34 ||
				u32(20) != expected_cluster_edges || u32(24) != expected_zone_edges) return false;
			std::array<link, total_points> checked{};
			std::array<std::uint16_t, expected_cluster_edges> clusters{};
			std::array<std::uint16_t, expected_zone_edges> zones{};
			for (std::size_t i = 0; i < total_points; ++i)
			{
				const auto at = header + i * disk_link;
				const auto cluster_start = u32(at);
				const auto cluster_count = u16(at + 4);
				const auto zone_start = u32(at + 6);
				const auto zone_count = u16(at + 10);
				// Every original world point has one source cluster and zone.
				// Every original cache point has one direct zone and no cluster.
				// The last original type-5 point is truly unassigned.
				const auto expected_cluster_count = i < world_points ? 1 : 0;
				const auto expected_zone_count = i == quest_index ? 0 : 1;
				if (cluster_count != expected_cluster_count || zone_count != expected_zone_count ||
					cluster_start > expected_cluster_edges || zone_start > expected_zone_edges ||
					cluster_count > expected_cluster_edges - cluster_start ||
					zone_count > expected_zone_edges - zone_start) return false;
				checked[i] = {cluster_start, cluster_count, zone_start, zone_count};
			}
			const auto data = header + total_points * disk_link;
			for (std::size_t i = 0; i < clusters.size(); ++i)
			{
				clusters[i] = u16(data + i * 2);
				if (clusters[i] >= 1227) return false;
			}
			const auto zone_data = data + clusters.size() * 2;
			for (std::size_t i = 0; i < zones.size(); ++i)
			{
				zones[i] = u16(zone_data + i * 2);
				if (zones[i] >= 34) return false;
			}
			if (checked[quest_index].cluster_count || checked[quest_index].zone_count) return false;
			links_ = checked;
			clusters_ = clusters;
			zones_ = zones;
			loaded_ = true;
			return true;
		}
		bool loaded() const noexcept { return loaded_; }
		std::span<const std::uint16_t> clusters(std::size_t point_index) const noexcept
		{
			if (!loaded_ || point_index >= total_points) return {};
			const auto& row = links_[point_index];
			return {clusters_.data() + row.cluster_start, row.cluster_count};
		}
		std::span<const std::uint16_t> zones(std::size_t point_index) const noexcept
		{
			if (!loaded_ || point_index >= total_points) return {};
			const auto& row = links_[point_index];
			return {zones_.data() + row.zone_start, row.zone_count};
		}
	private:
		struct link { std::uint32_t cluster_start{}; std::uint16_t cluster_count{};
			std::uint32_t zone_start{}; std::uint16_t zone_count{}; };
		std::array<link, total_points> links_{};
		std::array<std::uint16_t, world_points> clusters_{};
		std::array<std::uint16_t, total_points - 1> zones_{};
		bool loaded_{};
	};
}
