#include "diagnostics/escape/escape_shapes.h"
#include "diagnostics/escape/escape_shape_bindings.h"
#include "diagnostics/night/night_files.h"
#include "diagnostics/night/night_physics.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
namespace escape_shapes
{
	namespace
	{
		struct Record
		{
			const char* zone;
			const char* name;
			const char* sound;
			std::uint32_t bytes;
			std::array<std::uint8_t, 24> scalars;
			std::array<std::uint8_t, 32> digest;
			std::int32_t interval, limit;
		};

		constexpr Record catalog[]{
#include "diagnostics/escape/escape_shapes.inc"
		};

		struct Header
		{
			const char* name;
			std::uint32_t bytes, padding;
			const void* data;
			const char* sound;
			std::array<std::uint8_t, 16> scalars;
		};

		static_assert(sizeof(Header) == 48);
		using RegisterFn = void (*)(uintptr_t*);
		using FreeFn = void (*)(int, uintptr_t);
		std::atomic<RegisterFn> originalRegister{};
		std::atomic<FreeFn> originalFree{};
		uintptr_t imageBase{};
		std::mutex mutex;
		std::unordered_map<uintptr_t, const Record*> admitted;

		[[noreturn]] void invalid(const char* what)
		{
			LOG_ERR("EscapeShapes", "%s", what);
			RaiseFailFastException(nullptr, nullptr, 0);
			std::terminate();
		}

		bool same_string(const char* actual, const char* expected)
		{
			if (!actual) return !*expected;
			std::array<char, 256> text{};
			const auto count = safemem::ReadString(actual, text.data(), text.size());
			return count < text.size() - 1 && !std::strcmp(text.data(), expected);
		}

		bool header_matches(const Header& h, const Record& r)
		{
			std::array<std::uint8_t, 24> scalars{};
			std::memcpy(scalars.data(), &h.bytes, 8);
			std::memcpy(scalars.data() + 8, h.scalars.data(), 16);
			return h.bytes == r.bytes && scalars == r.scalars && same_string(h.name, r.name) && same_string(h.sound, r.sound);
		}

		bool data_matches(const Header& h, const Record& r)
		{
			if (!header_matches(h, r) || !h.data) return false;
			BCRYPT_ALG_HANDLE algorithm{};
			BCRYPT_HASH_HANDLE hash{};
			bool good = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
				BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
			std::array<std::uint8_t, 4096> buffer{};
			std::array<std::uint8_t, 32> digest{};
			for (std::size_t offset = 0; good && offset < r.bytes;)
			{
				const auto n = (std::min)(buffer.size(), std::size_t(r.bytes) - offset);
				SIZE_T read{};
				good = ReadProcessMemory(GetCurrentProcess(), static_cast<const std::uint8_t*>(h.data) + offset, buffer.data(), n, &read) &&
					read == n && BCryptHashData(hash, buffer.data(), ULONG(n), 0) >= 0;
				offset += n;
			}
			if (good) good = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) >= 0 && digest == r.digest;
			if (hash) BCryptDestroyHash(hash);
			if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
			return good;
		}

		const Record* capture(const char* zone, const Header& source)
		{
			if (!night_files::MapEnabled("mp_escape2") || !zone || (std::strcmp(zone, "mp_escape2") && std::strcmp(zone, "ww_mp_escape2")))
				return nullptr;
			if (!source.name) invalid("converted shape has no name");
			char name[256]{};
			safemem::ReadString(source.name, name, sizeof(name));
			// Original comma roots reference shared assets, supplying no shape data or
			// timing fields of their own. Preserve native resolution without ownership.
			if (name[0] == ',') return nullptr;
			for (const auto& r : catalog)
				if (!std::strcmp(r.zone, zone) && !std::strcmp(r.name, name))
				{
					if (!data_matches(source, r)) invalid("converted shape differs from its authenticated source record");
					return &r;
				}
			invalid("uncatalogued shape in authenticated Escape zone");
		}

		void register_impl(uintptr_t* slot)
		{
			uintptr_t pointer{};
			Header before{};
			if (!safemem::ReadBytes(slot, &pointer, sizeof(pointer)) || !pointer ||
				!safemem::ReadBytes(reinterpret_cast<void*>(pointer), &before, sizeof(before)))
				invalid("native shape registration has an unreadable input");
			const auto zone = reinterpret_cast<const char* (*)()>(imageBase + binding::CurrentZone.rva)();
			const auto* record = capture(zone, before);
			originalRegister.load(std::memory_order_acquire)(slot);
			if (!record) return;
			Header after{};
			if (!safemem::ReadBytes(slot, &pointer, sizeof(pointer)) || !pointer ||
				!safemem::ReadBytes(reinterpret_cast<void*>(pointer), &after, sizeof(after)))
				invalid("native shape registration has an unreadable result");
			// The DB owns/copies the header and may fix up Havok data during creation.
			// Admit only the returned header that retains the verified input's data;
			// a resolved pre-existing stock header must not acquire rules by name.
			if (after.data != before.data)
			{
				LOG_INFO("EscapeShapes", "native DB retained an existing shape for %s; converted timing not admitted", record->name);
				return;
			}
			if (!header_matches(after, *record)) invalid("native shape registration changed its verified scalar layout");
			std::lock_guard lock(mutex);
			if (const auto found = admitted.find(pointer); found != admitted.end())
			{
				const auto* old = found->second;
				if (old->interval != record->interval || old->limit != record->limit || old->digest != record->digest)
					invalid("native shape identity reused with conflicting source rules");
				return;
			}
			if (!night_physics::RegisterShape(pointer, { record->interval, record->limit, night_physics::MapScope::Escape130 }))
				invalid("verified shape rule registration failed");
			admitted.emplace(pointer, record);
			LOG_INFO(
				"EscapeShapes", "registered original timing shape=%s interval=%d limit=%d", record->name, record->interval, record->limit);
		}

		void free(int type, uintptr_t pointer)
		{
			if (type == 5)
			{
				std::lock_guard lock(mutex);
				if (admitted.erase(pointer)) night_physics::UnregisterShape(pointer);
			}
			// Native type dispatch and pool free-list writes retain their ownership.
			originalFree.load(std::memory_order_acquire)(type, pointer);
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (!night_files::MapEnabled("mp_escape2")) return hook::Status::Installed;
		if (originalRegister.load(std::memory_order_acquire)) return imageBase == base ? hook::Status::Installed : hook::Status::Failed;
		for (const auto& at : { binding::Register, binding::Free, binding::CurrentZone })
		{
			std::array<std::uint8_t, 16> observed{};
			if (!safemem::RegionExecutable(reinterpret_cast<void*>(base + at.rva)) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(base + at.rva), observed.data(), observed.size()) || observed != at.bytes)
				return hook::Status::Failed;
		}
		imageBase = base;
		hook::Batch batch;
		batch.Add(
			reinterpret_cast<void*>(base + binding::Register.rva), &register_impl, binding::Register.bytes.data(), 16, originalRegister);
		batch.Add(reinterpret_cast<void*>(base + binding::Free.rva), &free, binding::Free.bytes.data(), 16, originalFree);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed)
			LOG_INFO("EscapeShapes", "verified original shape timing catalog and native header lifetime hooks installed");
		return status;
	}
}
