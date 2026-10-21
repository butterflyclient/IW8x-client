#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
namespace night_builtins
{
	// Exact Replay initializer 12E0EE0 and reset 1316ECD..1316EFC.
	// Keep this storage alive for the process: native contexts share both tables.
	struct Registration
	{
		std::uint32_t id{}, pad{};
		std::uintptr_t callback{}, flags{};
	};

	struct Descriptor
	{
		const Registration* records{};
		std::size_t count{};
	};

	static_assert(sizeof(Registration) == 24 && sizeof(Descriptor) == 16);

	class Tables
	{
	public:
		static constexpr std::uint32_t First = 1, NativeEnd = 0x419, CapacityEnd = 0x424;
		static constexpr std::uint32_t MethodFirst = 0x8000, NativeMethodEnd = 0x8787, MethodCapacityEnd = 0x87B9;
		static constexpr std::size_t NativeCount = NativeEnd - First, NativeMethodCount = NativeMethodEnd - MethodFirst;

		template <class T>
		static T Read(const void* context, std::size_t offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const unsigned char*>(context) + offset, sizeof(value));
			return value;
		}

		template <class T>
		static void Write(void* context, std::size_t offset, T value)
		{
			std::memcpy(static_cast<unsigned char*>(context) + offset, &value, sizeof(value));
		}

	private:
		std::array<std::uintptr_t, CapacityEnd - First> functions_{}, functionProviders_{};
		std::array<std::uintptr_t, MethodCapacityEnd - MethodFirst> methods_{}, methodProviders_{};
		std::uint32_t functionEnd_ = NativeEnd, methodEnd_ = NativeMethodEnd;
		bool sealed_ = false, copied_ = false;

		std::uintptr_t Provider(std::uint32_t id) const
		{
			if (id >= First && id < CapacityEnd) return functionProviders_[id - First];
			if (id >= MethodFirst && id < MethodCapacityEnd) return methodProviders_[id - MethodFirst];
			return 0;
		}

		void Restore()
		{
			// BeginLoadScripts clears the complete context-sized arrays, including
			// providers. Refill during the exclusive native registration phase.
			for (std::size_t i = 0; i < functionProviders_.size(); ++i)
				if (functionProviders_[i]) functions_[i] = functionProviders_[i];
			for (std::size_t i = 0; i < methodProviders_.size(); ++i)
				if (methodProviders_[i]) methods_[i] = methodProviders_[i];
		}

	public:
		bool Owns(const void* context) const
		{
			return sealed_ && context && Read<std::uintptr_t>(context, 0xC90) == reinterpret_cast<std::uintptr_t>(functions_.data()) &&
				Read<std::uintptr_t>(context, 0xC98) == reinterpret_cast<std::uintptr_t>(methods_.data()) &&
				Read<std::uint32_t>(context, 0xC78) == functionEnd_ && Read<std::uint32_t>(context, 0xC84) == methodEnd_ &&
				Read<std::uint32_t>(context, 0xC7C) == functionEnd_ - First &&
				Read<std::uint32_t>(context, 0xC88) == methodEnd_ - MethodFirst;
		}

		bool Reserved(bool method, std::uint32_t slot) const
		{
			const auto id = (method ? NativeMethodEnd : NativeEnd) + slot;
			return Provider(id) != 0;
		}

		bool Add(std::uint32_t id, std::uintptr_t callback)
		{
			if (sealed_ || !callback || Provider(id)) return false;
			if (id >= NativeEnd && id < CapacityEnd)
			{
				functionProviders_[id - First] = callback;
				functionEnd_ = std::max(functionEnd_, id + 1);
				return true;
			}
			// 874F is the sole required hole in the original method prefix.
			// Exact registries prove it absent. Runtime descriptors are checked too.
			if ((id >= NativeMethodEnd && id < MethodCapacityEnd) || id == 0x874F)
			{
				methodProviders_[id - MethodFirst] = callback;
				methodEnd_ = std::max(methodEnd_, id + 1);
				return true;
			}
			return false;
		}

		void Seal()
		{
			sealed_ = true;
		}

		bool ValidDescriptor(const Descriptor* descriptor) const
		{
			if (!sealed_ || !descriptor || descriptor->count > 0x10000 || (descriptor->count && !descriptor->records)) return false;
			for (std::size_t i = 0; i < descriptor->count; ++i)
			{
				const auto& row = descriptor->records[i];
				if (!row.callback || Provider(row.id) ||
					!((row.id >= First && row.id < NativeEnd) || (row.id >= MethodFirst && row.id < NativeMethodEnd)))
					return false;
			}
			return true;
		}

		bool Adopt(void* context, std::uintptr_t* nativeFunctions, std::uintptr_t* nativeMethods, bool initialized = false)
		{
			if (!sealed_ || !context || !nativeFunctions || !nativeMethods || Read<std::uint32_t>(context, 0xC74) != First ||
				Read<std::uint32_t>(context, 0xC80) != MethodFirst)
				return false;
			const auto fp = Read<std::uintptr_t>(context, 0xC90), mp = Read<std::uintptr_t>(context, 0xC98);
			const bool native =
				fp == reinterpret_cast<std::uintptr_t>(nativeFunctions) && mp == reinterpret_cast<std::uintptr_t>(nativeMethods);
			const bool owned =
				fp == reinterpret_cast<std::uintptr_t>(functions_.data()) && mp == reinterpret_cast<std::uintptr_t>(methods_.data());
			if ((!native && !owned) || (initialized && !native)) return false;
			const auto fe = native ? NativeEnd : functionEnd_, me = native ? NativeMethodEnd : methodEnd_;
			if (Read<std::uint32_t>(context, 0xC78) != fe || Read<std::uint32_t>(context, 0xC7C) != fe - First ||
				Read<std::uint32_t>(context, 0xC84) != me || Read<std::uint32_t>(context, 0xC88) != me - MethodFirst)
				return false;
			if (initialized) copied_ = false;
			if (!copied_)
			{
				if (!native) return false;
				for (std::size_t i = 0; i < NativeMethodCount; ++i)
					if (methodProviders_[i] && nativeMethods[i] && nativeMethods[i] != methodProviders_[i]) return false;
				std::copy_n(nativeFunctions, NativeCount, functions_.data());
				std::copy_n(nativeMethods, NativeMethodCount, methods_.data());
				copied_ = true;
			}
			Restore();
			Write(context, 0xC78, functionEnd_);
			Write(context, 0xC7C, functionEnd_ - First);
			Write(context, 0xC84, methodEnd_);
			Write(context, 0xC88, methodEnd_ - MethodFirst);
			Write(context, 0xC90, reinterpret_cast<std::uintptr_t>(functions_.data()));
			Write(context, 0xC98, reinterpret_cast<std::uintptr_t>(methods_.data()));
			return true;
		}

		void Mirror(std::uintptr_t* nativeFunctions, std::uintptr_t* nativeMethods) const
		{
			std::copy_n(functions_.data(), NativeCount, nativeFunctions);
			std::copy_n(methods_.data(), NativeMethodCount, nativeMethods);
		}
	};
}
