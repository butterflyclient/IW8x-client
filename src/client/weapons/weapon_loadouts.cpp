#include "client/weapons/weapon_loadouts.h"
#include "diagnostics/weapons/weapon_files.h"
#include "client/weapons/weapon_rules.h"
#include "client/weapons/weapon_ammo.h"
#include "client/weapons/weapon_recoil.h"
#include "client/weapons/weapon_hitlocation.h"
#include "client/weapons/weapon_lowammo.h"
#include "client/weapons/weapon_capacity.h"
#include "client/weapons/weapon_spread.h"
#include "common/embedded_assets.h"
#include "engine/resident_string_tables.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <stdexcept>
namespace weapon_loadouts
{
	namespace
	{
		using json = nlohmann::json;
		using Table = resident_tables::Table;

		struct Hash
		{
			uint32_t value, index;
		};

		struct Enum
		{
			const char* name;
			int count, pad;
			const char* const* members;
			const Hash* hashes;
			int hashCount, hashCapacity;
		};

		static_assert(sizeof(Enum) == 40 && offsetof(Enum, members) == 16 && offsetof(Enum, hashes) == 24);

		struct TableEdit
		{
			resident_tables::Asset* target{};
			resident_tables::Asset replacement{};
			std::vector<std::string> strings;
			std::vector<const char*> pointers;
			std::vector<uint16_t> cells;
			std::vector<uint32_t> hashes;
		};

		struct EnumEdit
		{
			Enum* target{};
			Enum replacement{};
			std::vector<std::string> strings;
			std::vector<const char*> pointers;
			std::vector<Hash> hashes;
		};

		std::vector<std::unique_ptr<TableEdit>> tables;
		std::vector<std::unique_ptr<EnumEdit>> enums;
		std::map<std::string, std::string, std::less<>> categories;
		std::atomic<bool> ready{ false };
		bool failed = false;

		void require(bool b, const char* why)
		{
			if (!b) throw std::runtime_error(why);
		}

		template <class T>
		T read(const void* p)
		{
			T v{};
			require(safemem::ReadBytes(p, &v, sizeof(v)), "unreadable native metadata");
			return v;
		}

		std::string string_impl(const char* p)
		{
			char out[256]{};
			auto n = safemem::ReadString(p, out, sizeof(out));
			require(n && n < 255, "invalid metadata name");
			return out;
		}

		bool writable(void* p, size_t n)
		{
			return safemem::RegionWritable(p) && safemem::RegionWritable(static_cast<char*>(p) + n - 1);
		}

		uint32_t hash_string(std::string_view s, bool lower)
		{
			uint32_t h = 0;
			for (unsigned char c : s)
			{
				if (lower && c >= 'A' && c <= 'Z') c += 'a' - 'A';
				h = 31 * h + c;
			}
			return h;
		}

		std::unique_ptr<TableEdit> make_table(resident_tables::Asset* target, const Table& rows)
		{
			auto out = std::make_unique<TableEdit>();
			out->target = target;
			out->replacement = *target;
			std::map<std::string, uint16_t> ids;
			require(!rows.empty() && rows.size() < 20000, "invalid table extent");
			for (const auto& row : rows)
			{
				require(row.size() == size_t(target->columns), "table column layout changed");
				for (const auto& value : row)
				{
					auto at = ids.find(value);
					if (at == ids.end())
					{
						require(ids.size() < 65535, "string table value limit");
						auto id = uint16_t(out->strings.size());
						out->strings.push_back(value);
						out->hashes.push_back(hash_string(value, true));
						at = ids.emplace(value, id).first;
					}
					out->cells.push_back(at->second);
				}
			}
			for (const auto& s : out->strings)
				out->pointers.push_back(s.c_str());
			auto& h = out->replacement;
			h.rows = int(rows.size());
			h.values = int(out->strings.size());
			h.cells = out->cells.data();
			h.hashes = out->hashes.data();
			h.strings = out->pointers.data();
			require(writable(target, sizeof(*target)), "native table is not writable");
			return out;
		}
	}

	bool Ready()
	{
		return ready.load(std::memory_order_acquire);
	}

	const char* Category(std::string_view reference)
	{
		if (!Ready()) return nullptr;
		const auto at = categories.find(reference);
		return at == categories.end() ? nullptr : at->second.c_str();
	}

	bool Prepare(uintptr_t base)
	{
		if (!weapon_files::Enabled() || Ready()) return true;
		if (failed) return false;
		try
		{
			const auto data = embedded_assets::Find("data/weapon_pilot_menu.json");
			require(!data.empty(), "missing embedded menu metadata");
			const auto spec = json::parse(data);
			auto pool = read<resident_tables::Asset*>(reinterpret_cast<void*>(base + 0xB8F5A80 + 54 * 8));
			auto count = read<int>(reinterpret_cast<void*>(base + 0x23FBBA0 + 54 * 4));
			require(count > 0 && count <= 8192, "native table pool extent");
			resident_tables::Reader reader(pool, count);
			std::vector<std::unique_ptr<TableEdit>> pendingTables;
			std::vector<std::unique_ptr<EnumEdit>> pendingEnums;
			std::map<std::string, std::string, std::less<>> pendingCategories;
			for (const auto& patch : spec.at("tables"))
			{
				const auto name = patch.at("name").get<std::string>();
				const auto old = patch.at("original").get<Table>();
				require(reader.Read(name) == old, "resident table differs from pinned TU19 original");
				if (name == "mp/statstable.csv")
				{
					std::set<std::string> nativeCategories;
					for (const auto& row : old)
						if (row.size() > 4) nativeCategories.insert(row[1]);
					const auto added = patch.at("append").get<Table>();
					for (const auto& gun : spec.at("weapons"))
					{
						const auto ref = gun.at("ref").get<std::string>();
						require(!ref.empty() && ref.size() < 64, "invalid ported weapon reference");
						require(std::none_of(old.begin(), old.end(),
									[&](const auto& row)
									{
										return row.size() > 4 && row[4] == ref;
									}),
							"ported weapon shadows a stock reference");
						const auto at = std::find_if(added.begin(), added.end(),
							[&](const auto& row)
							{
								return row.size() > 4 && row[4] == ref;
							});
						require(std::count_if(added.begin(), added.end(),
									[&](const auto& row)
									{
										return row.size() > 4 && row[4] == ref;
									}) == 1,
							"ambiguous ported weapon row");
						require(at != added.end() && !(*at)[1].empty() && nativeCategories.contains((*at)[1]),
							"ported weapon has no native recipe category");
						require(pendingCategories.emplace(ref, (*at)[1]).second, "duplicate ported weapon reference");
					}
				}
				resident_tables::Asset* target = nullptr;
				for (int i = 0; i < count; ++i)
				{
					char n[256]{};
					safemem::ReadString(pool[i].name, n, sizeof(n));
					if (!_stricmp(n, name.c_str()))
					{
						require(!target, "duplicate resident table");
						target = pool + i;
					}
				}
				require(target, "resident menu table missing");
				auto rows = old;
				for (const auto& row : patch.at("append"))
					rows.push_back(row.get<std::vector<std::string>>());
				pendingTables.push_back(make_table(target, rows));
			}
			const auto load = reinterpret_cast<const uint8_t* (*)(const char*)>(base + 0x2051180);
			for (const auto& patch : spec.at("loadout_enums"))
			{
				auto file = load(patch.at("name").get<std::string>().c_str());
				require(file, "DDL asset missing");
				auto def = read<const uint8_t*>(file + 8);
				require(def, "DDL definition missing");
				require(read<uint32_t>(def + 8) == patch.at("version").get<uint32_t>() &&
						read<uint32_t>(def + 36) == patch.at("bytes").get<uint32_t>(),
					"DDL layout/version changed");
				auto list = read<Enum*>(def + 56);
				auto index = patch.at("enum_index").get<int>();
				require(index >= 0 && index < read<int>(def + 64), "DDL enum index out of range");
				auto out = std::make_unique<EnumEdit>();
				out->target = list + index;
				out->replacement = read<Enum>(out->target);
				auto& e = out->replacement;
				auto old = patch.at("original").get<std::vector<std::string>>();
				require(string_impl(e.name) == "LoadoutWeapon" && e.count == 61 && old.size() == 61 && e.hashCount == 61 &&
						e.hashCapacity == 61,
					"DDL enum layout changed");
				for (int i = 0; i < e.count; ++i)
				{
					require(string_impl(read<const char*>(e.members + i)) == old[i], "existing loadout enum changed");
					const auto h = read<Hash>(e.hashes + i);
					require(h.index < old.size() && h.value == hash_string(old[h.index], false), "native DDL hash mismatch");
				}
				out->strings = old;
				for (const auto& gun : spec.at("weapons"))
					out->strings.push_back(gun.at("ref").get<std::string>());
				require(!spec.at("weapons").empty() && out->strings.size() <= 256, "loadout enum exceeds reviewed eight-bit allocation");
				for (uint32_t i = 0; i < out->strings.size(); ++i)
				{
					out->pointers.push_back(out->strings[i].c_str());
					out->hashes.push_back({ hash_string(out->strings[i], false), i });
				}
				std::sort(out->hashes.begin(), out->hashes.end(),
					[](auto a, auto b)
					{
						return a.value < b.value;
					});
				for (size_t i = 1; i < out->hashes.size(); ++i)
					require(out->hashes[i - 1].value != out->hashes[i].value, "DDL enum hash collision");
				e.count = e.hashCount = e.hashCapacity = int(out->strings.size());
				e.members = out->pointers.data();
				e.hashes = out->hashes.data();
				require(writable(out->target, sizeof(Enum)), "native DDL enum is not writable");
				pendingEnums.push_back(std::move(out));
			}
			require(pendingEnums.size() == 2 && pendingTables.size() == 3, "incomplete native menu extension");
			require(pendingCategories.size() == spec.at("weapons").size(), "incomplete native weapon category map");
			require((spec.contains("recoil_modifiers") && !spec.at("recoil_modifiers").empty()) ==
					spec.value("requires_recoil_modifiers", false),
				"recoil metadata and native bridge requirement disagree");
			require((spec.contains("hitlocation_modifiers") && !spec.at("hitlocation_modifiers").empty()) ==
					spec.value("requires_hitlocation_modifiers", false),
				"hit-location metadata and native bridge requirement disagree");
			require((spec.contains("lowammo_modifiers") && !spec.at("lowammo_modifiers").empty()) ==
					spec.value("requires_lowammo_warning", false),
				"low-ammo metadata and native bridge requirement disagree");
			require((spec.contains("capacity_modifiers") && !spec.at("capacity_modifiers").empty()) ==
					spec.value("requires_capacity_additive", false),
				"additive-capacity metadata and native bridge requirement disagree");
			require((spec.contains("spread_modifiers") && !spec.at("spread_modifiers").empty()) ==
					spec.value("requires_spread_modifiers", false),
				"shotgun-spread metadata and native bridge requirement disagree");
			require(weapon_rules::Install(base) == hook::Status::Installed, "native weapon recipe bridge mismatch");
			if (spec.value("requires_capacity_additive", false))
				require(weapon_capacity::Install(base, spec.dump()) == hook::Status::Installed, "native additive-capacity bridge mismatch");
			if (spec.value("requires_ammo_inheritance", false))
				require(weapon_ammo::Install(base) == hook::Status::Installed, "native ammo inheritance bridge mismatch");
			if (spec.value("requires_recoil_modifiers", false))
				require(weapon_recoil::Install(base, spec.dump()) == hook::Status::Installed, "native recoil modifier bridge mismatch");
			if (spec.value("requires_hitlocation_modifiers", false))
				require(weapon_hitlocation::Install(base, spec.dump()) == hook::Status::Installed,
					"native hit-location modifier bridge mismatch");
			if (spec.value("requires_lowammo_warning", false))
				require(weapon_lowammo::Install(base, spec.dump()) == hook::Status::Installed, "native low-ammo warning bridge mismatch");
			if (spec.value("requires_spread_modifiers", false))
				require(weapon_spread::Install(base, spec.dump()) == hook::Status::Installed, "native shotgun-spread bridge mismatch");
			// All reads/allocations/validation precede the first native write. These
			// code_post_gfx assets are resident; DLL-owned backing arrays live until
			// process exit. Existing DDL offsets, bit widths, versions and IDs stay.
			tables = std::move(pendingTables);
			enums = std::move(pendingEnums);
			categories = std::move(pendingCategories);
			for (const auto& e : enums)
				*e->target = e->replacement;
			for (const auto& t : tables)
				*t->target = t->replacement;
			ready.store(true, std::memory_order_release);
			LOG_INFO("WeaponLoadouts",
				"added %u ported weapon references: 3 native menu tables; 2 loadout enums 61->%u; stock IDs and bit layout preserved",
				unsigned(categories.size()), unsigned(enums.front()->strings.size()));
			return true;
		}
		catch (const std::exception& e)
		{
			failed = true;
			LOG_ERR("WeaponLoadouts", "menu admission failed: %s", e.what());
			return false;
		}
	}
}
