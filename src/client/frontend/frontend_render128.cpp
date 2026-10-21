#include "client/frontend/frontend_render128.h"
#include "network/native_network.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <atomic>
#include <mutex>
#include <set>
#include <string>
namespace frontend_render128
{
	namespace
	{
		using Fn = void (*)(uintptr_t*);
		std::atomic<Fn> original{ nullptr };
		std::atomic<unsigned> remaining{ 512 };
		std::atomic<ULONGLONG> deadline{ 0 };
		std::mutex mutex;
		std::set<std::pair<uintptr_t, uintptr_t>> seen;

		template <class T>
		T read(uintptr_t at)
		{
			T result{};
			safemem::ReadBytes(reinterpret_cast<void*>(at), &result, sizeof(result));
			return result;
		}

		std::string name_impl(uintptr_t at)
		{
			char buffer[192]{};
			safemem::ReadString(reinterpret_cast<const char*>(read<uintptr_t>(at)), buffer, sizeof(buffer));
			return buffer;
		}

		void sample(uintptr_t* context)
		{
			original.load(std::memory_order_acquire)(context);
			if (remaining.load(std::memory_order_relaxed))
			{
				const auto end = deadline.load(std::memory_order_relaxed);
				if (end && GetTickCount64() > end)
				{
					remaining = 0;
					return;
				}
				const auto state = read<uintptr_t>(reinterpret_cast<uintptr_t>(context) + 8);
				const auto material = read<uintptr_t>(state + 4408), pass = read<uintptr_t>(state + 4416);
				const auto name = name_impl(material);
				if (name.starts_with("tw/") || name.starts_with("wc4/") || name.starts_with("w4/") || name.starts_with("mo/") ||
					name.starts_with("m2o/") || name.starts_with("mco/") || name.starts_with("elcq/"))
				{
					ULONGLONG unset = 0;
					deadline.compare_exchange_strong(unset, GetTickCount64() + 10000);
					std::lock_guard guard(mutex);
					if (remaining && seen.emplace(material, pass).second)
					{
						--remaining;
						const auto technique = name_impl(pass);
						const auto setName = name_impl(read<uintptr_t>(material + 64));
						const auto vertexName = name_impl(read<uintptr_t>(pass + 88));
						const auto pixelName = name_impl(read<uintptr_t>(pass + 112));
						const auto textures = read<uintptr_t>(material + 72);
						const unsigned count = read<uint8_t>(material + 28);
						LOG_INFO("Render128Probe", "material=%s pass=%s textures=%u counts=%u/%u/%u/%u", name.c_str(), technique.c_str(),
							count, read<uint8_t>(pass + 120), read<uint8_t>(pass + 121), read<uint8_t>(pass + 122),
							read<uint8_t>(pass + 123));
						LOG_INFO("Render128Probe", "set=%s vertex=%s pixel=%s", setName.c_str(), vertexName.c_str(), pixelName.c_str());
						for (unsigned i = 0; i < count && i < 16; ++i)
						{
							const auto image = read<uintptr_t>(textures + i * 16 + 8);
							LOG_INFO("Render128Probe", "texture slot=%u image=%s textureId=%u", read<uint8_t>(textures + i * 16),
								name_impl(image).c_str(), read<unsigned>(image + 16));
						}
						if (name == "tw/concrete_slabs_02_stained")
						{
							const auto size = read<uint16_t>(pass + 128);
							std::string values;
							constexpr char hex[] = "0123456789abcdef";
							uint8_t data[1024]{};
							const auto bytes = (std::min)(unsigned(size), unsigned(sizeof(data)));
							if (bytes && safemem::ReadBytes(reinterpret_cast<void*>(state + 6448), data, bytes))
							{
								for (unsigned i = 0; i < bytes; ++i)
								{
									values += hex[data[i] >> 4];
									values += hex[data[i] & 15];
								}
								LOG_INFO("Render128Probe", "floor stable bytes=%u data=%s", size, values.c_str());
							}
						}
					}
				}
			}
		}
	}

	hook::Status Install(uintptr_t base)
	{
		wchar_t path[MAX_PATH]{}, scene[32]{};
		if (!native_network::ConfigurationPath(path, MAX_PATH) || !GetPrivateProfileIntW(L"ui", L"frontend_render_probe", 0, path))
			return hook::Status::Installed;
		GetPrivateProfileStringW(L"ui", L"frontend_scene", L"", scene, 32, path);
		if (wcscmp(scene, L"1.28-test") && wcscmp(scene, L"1.28-halloween") && wcscmp(scene, L"stock")) return hook::Status::Installed;
		constexpr uint8_t bytes[] = { 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0xa0, 0, 0, 0, 0x48, 0x8b, 0xf1 };
		const auto result = hook::Install(reinterpret_cast<void*>(base + 0x1983160), sample, bytes, sizeof(bytes), original);
		LOG_INFO("Render128Probe", "bounded read-only material/pass probe status=%d", int(result));
		return result;
	}
}
