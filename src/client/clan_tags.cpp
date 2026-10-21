#include "client/clan_tags.h"

#include "engine/addresses.hpp"

#include "common/console.h"
namespace clan_tags
{
	namespace
	{
		using EditFn = void (*)(unsigned, bool, const char*);
		using CheckFn = bool (*)(unsigned, const char*);
		std::atomic<EditFn> g_edit{ nullptr };
		std::atomic<CheckFn> g_profanity{ nullptr }, g_developerTag{ nullptr };
		thread_local bool g_editingClan = false;

		void Edit(unsigned controller, bool accepted, const char* text)
		{
			const bool previous = g_editingClan;
			g_editingClan = true;
			g_edit.load()(controller, accepted, text);
			g_editingClan = previous;
		}

		bool Profanity(unsigned controller, const char* text)
		{
			return !g_editingClan && g_profanity.load()(controller, text);
		}

		bool DeveloperTag(unsigned controller, const char*)
		{
			return controller <= 1;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		// The native setter checks developer-tag eligibility a second time while
		// saving. Preserve its length, whitespace and control-character handling;
		// change only the requested profanity/Activision-tag eligibility checks.
		const uint8_t edit[] = { 0x84, 0xd2, 0x0f, 0x84, 0xb7, 0, 0, 0, 0x48, 0x89, 0x5c, 0x24, 0x20 };
		const uint8_t profanity[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x8b, 0xd9 };
		const uint8_t developer[] = { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x81, 0xec, 0xa0, 0, 0, 0 };
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0x1ba06d0), &DeveloperTag, developer, sizeof(developer), g_developerTag);
		batch.Add(reinterpret_cast<void*>(base + 0x16094c0), &Profanity, profanity, sizeof(profanity), g_profanity);
		batch.Add(reinterpret_cast<void*>(base + 0x1b9b9c0), &Edit, edit, sizeof(edit), g_edit);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed) LOG_INFO("ClanTag", "native developer tags allowed; clan edit profanity check disabled");
		return status;
	}
}
