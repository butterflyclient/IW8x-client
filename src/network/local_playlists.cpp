#include "network/local_playlists.h"

#include "common/embedded_assets.h"

#include "engine/addresses.hpp"

#include "network/playlist_document.h"

#include <fstream>
#include <vector>
namespace playlists::local
{
	namespace
	{
		constexpr size_t kTextCapacity = 0x50000;
		thread_local uintptr_t g_base = 0;

		template <class T>
		T Function(uintptr_t rva)
		{
			return reinterpret_cast<T>(g_base + rva);
		}

		struct ZStream
		{
			const unsigned char* nextIn;
			uint32_t availableIn, totalIn;
			unsigned char* nextOut;
			uint32_t availableOut, totalOut;
			const char* message;
			void* state;
			void* alloc;
			void* free;
			void* opaque;
			int32_t dataType;
			uint32_t adler, reserved;
		};

		static_assert(sizeof(ZStream) == 88 && offsetof(ZStream, totalOut) == 28 && offsetof(ZStream, opaque) == 64);

		std::string Inflate(const std::vector<unsigned char>& compressed)
		{
			if (compressed.empty() || compressed.size() > kTextCapacity)
				throw std::runtime_error("compressed playlist is empty or too large");
			// zlib's game allocator takes a caller-owned 128 KiB scratch arena. It does
			// not require the publisher response or a game/global compressed buffer.
			std::vector<unsigned char> scratch(0x20000), decoded(kTextCapacity);
			ZStream stream{};
			stream.nextIn = compressed.data();
			stream.availableIn = static_cast<uint32_t>(compressed.size());
			stream.nextOut = decoded.data();
			stream.availableOut = static_cast<uint32_t>(decoded.size() - 1);
			Function<void (*)(ZStream*, void*, int)>(game::addresses::runtime::rva_0206b170)(
				&stream, scratch.data(), static_cast<int>(scratch.size()));
			const int init =
				Function<int (*)(ZStream*, const char*, int)>(game::addresses::runtime::rva_0206b040)(&stream, "1.1.4", sizeof(stream));
			if (init != 0)
			{
				Function<void (*)(ZStream*)>(game::addresses::runtime::rva_0206b1e0)(&stream);
				throw std::runtime_error("native inflateInit failed: " + std::to_string(init));
			}
			const int result = Function<int (*)(ZStream*, int)>(game::addresses::runtime::rva_0206ab70)(&stream, 2);
			const bool complete = result == 1 && stream.availableIn == 0 && stream.totalIn == compressed.size() && stream.totalOut > 0 &&
				stream.totalOut < decoded.size();
			const auto size = stream.totalOut;
			const int end = Function<int (*)(ZStream*)>(game::addresses::runtime::rva_0206afe0)(&stream);
			Function<void (*)(ZStream*)>(game::addresses::runtime::rva_0206b1e0)(&stream);
			if (!complete || end != 0)
				throw std::runtime_error(
					"native inflate rejected incomplete, corrupt, trailing, or oversized input; status=" + std::to_string(result));
			return std::string(reinterpret_cast<char*>(decoded.data()), size);
		}

	}

	playlist_catalog::Snapshot Load(uintptr_t base, const std::filesystem::path& path)
	{
		g_base = base;
		std::vector<unsigned char> bytes;
		if (path.empty())
		{
			const auto resource = embedded_assets::Find("data/playlists/unified_large_playlist_tu19.aggr");
			if (resource.empty()) throw std::runtime_error("embedded TU19 playlist is missing");
			bytes.assign(resource.begin(), resource.end());
		}
		else
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) throw std::runtime_error("cannot open .aggr: " + path.string());
			const auto length = file.tellg();
			if (length <= 0 || length > static_cast<std::streamoff>(kTextCapacity))
				throw std::runtime_error("compressed .aggr exceeds input limit");
			bytes.resize(static_cast<size_t>(length));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) throw std::runtime_error("short .aggr read");
		}
		auto text = Inflate(bytes);
		if (path.empty()) text = ExpandBundledCategories(text);
		return playlist_catalog::Validate(std::move(text));
	}
}
