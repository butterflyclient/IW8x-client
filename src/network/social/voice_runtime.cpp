#include "network/social/voice_runtime.h"
#include "network/social/chat_runtime.h"
#include "network/backend/frontend_backend.h"
#include "auth/offline_auth.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <array>
#include <atomic>
#include <intrin.h>
#include <map>
namespace voice_runtime
{
	namespace
	{
		uintptr_t base = 0;
		bool installed = false;
		using Gate = uintptr_t (*)(uintptr_t);
		using Restriction = bool (*)();
		std::atomic<Restriction> originalRestriction{ nullptr };
		std::atomic<Gate> originalAvailable{ nullptr }, originalAllowed{ nullptr }, originalBanned{ nullptr };

		struct Binding
		{
			uintptr_t rva;
			std::vector<uint8_t> expected;
		};

		const Binding bindings[]{
#include "network/social/voice_bindings.inc"
		};

		template <class T>
		T read(uintptr_t at)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(at), &value, sizeof(value));
			return value;
		}

		bool Restricted()
		{
			return frontend_backend::Required() ? !chat_runtime::VoicePermission() : originalRestriction.load()();
		}

		uintptr_t Available(uintptr_t self)
		{
			if (frontend_backend::Required() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x1BCFE2D)
				return chat_runtime::VoicePermission();
			return originalAvailable.load()(self);
		}

		uintptr_t Allowed(uintptr_t self)
		{
			if (frontend_backend::Required() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x1BCFE3E)
				return chat_runtime::VoicePermission();
			return originalAllowed.load()(self);
		}

		uintptr_t Banned(uintptr_t self)
		{
			if (frontend_backend::Required() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x1BCFE4F)
				return !chat_runtime::VoicePermission();
			return originalBanned.load()(self);
		}
	}

	void Pump()
	{
		if (!installed) return;
		const bool permitted = chat_runtime::VoicePermission();
		static int previous = -1;
		static uint64_t next = 0;
		const auto now = GetTickCount64();
		if (previous == int(permitted) && now < next) return;
		next = now + 1000;
		const bool changed = previous != int(permitted);
		previous = int(permitted);
		const auto local = offlineauth::GetIdentity().xuid;
		static std::map<std::pair<uintptr_t, int>, uint64_t> prior;
		std::map<std::pair<uintptr_t, int>, uint64_t> current;
		for (auto rva : { uintptr_t(0xC796480), uintptr_t(0xC73ADE0) })
		{
			const auto party = base + rva;
			if (!read<int>(party + 311116)) continue;
			const auto id = read<int>(party + 311052);
			if (id < 0 || id > 1) continue;
			bool localRegistered = false;
			for (int slot = 0; slot < 200; ++slot)
				if (reinterpret_cast<bool (*)(uintptr_t, int)>(base + 0x118E9B0)(party, slot))
				{
					const auto uid = read<uint64_t>(party + 488ull * slot + 2600);
					if (uid)
					{
						current[{ rva, slot }] = uid;
						if (uid == local) localRegistered = true;
					}
				}
			// Native allocation, codec, saved mic preference, recipient/team and
			// mute/block policy remain untouched. Re-evaluate only its own gate.
			if (changed && localRegistered) reinterpret_cast<void (*)(uintptr_t, int)>(base + 0x1BCFA60)(party, 0);
		}
		if (changed || current != prior)
		{
			LOG_INFO("Voice", "native registered talkers=%zu communications=%d", current.size(), int(permitted));
			prior = std::move(current);
		}
	}

	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (dedicated || installed) return hook::Status::Installed;
		base = image;
		for (const auto& b : bindings)
		{
			std::vector<uint8_t> actual(b.expected.size());
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + b.rva), actual.data(), actual.size()) || actual != b.expected)
			{
				LOG_ERR("Voice", "target binding mismatch RVA=%llx", b.rva);
				return hook::Status::NotReady;
			}
		}
		hook::Batch batch;
		auto add = [&]<class F>(uintptr_t rva, F fn, std::atomic<F>& original)
		{
			for (const auto& b : bindings)
				if (b.rva == rva) batch.Add(reinterpret_cast<void*>(base + rva), fn, b.expected.data(), b.expected.size(), original);
		};
		add(0x1BCF870, &Restricted, originalRestriction);
		add(0x165D550, &Available, originalAvailable);
		add(0x165D5E0, &Allowed, originalAllowed);
		add(0x165DDB0, &Banned, originalBanned);
		const auto status = batch.Commit();
		installed = status == hook::Status::Installed;
		return status;
	}
}
