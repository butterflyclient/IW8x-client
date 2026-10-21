#include "client/frontend/frontend_materials.h"
#include "common/embedded_assets.h"
#include "common/console.h"
#include "engine/safemem.h"
#include "thirdparty/json.hpp"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace frontend_materials
{
	namespace
	{
		using Bytes = std::vector<uint8_t>;
		constexpr size_t kPointers[] = { 0, 40, 80, 88, 96, 104, 112, 136, 176 };

		struct Shader
		{
			std::string name;
			Bytes program;
			std::array<uint8_t, 40> native{};
		};

		struct Technique
		{
			std::array<uint8_t, 184> header{};
			Bytes sourceArguments, arguments, state, argumentMap, pipeline;
			std::array<std::string, 4> sourceShaders;
			std::array<Shader*, 4> shaders{};
		};

		// The engine may retain pipeline jobs past DB_LoadFastfile, so backing storage
		// has process lifetime. Each load receives fresh mutable GPU-handle slots.
		std::unordered_map<std::string, std::unique_ptr<Shader>> g_shaders;
		std::unordered_map<std::string, Technique> g_techniques;
		std::vector<std::unique_ptr<Bytes>> g_pipelineStorage;
		std::mutex g_lock;
		using QueueFn = void(__fastcall*)(void*, const char*);
		std::atomic<QueueFn> g_original{ nullptr };
		using CompileFn = void(__fastcall*)(void*, const char*, int);
		std::atomic<CompileFn> g_compileOriginal{ nullptr };
		unsigned g_converted = 0, g_rejected = 0;
		uintptr_t g_base = 0;
		bool g_debugEnabled = false;
		std::atomic<bool> g_reported{ false };
		bool g_debugQueueConfigured = false;

		void configure_debug_queue()
		{
			if (!g_debugEnabled || g_debugQueueConfigured) return;
			ID3D12Device* device = nullptr;
			if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x10C79C20), &device, sizeof(device)) || !device) return;
			ID3D12InfoQueue* queue = nullptr;
			if (FAILED(device->QueryInterface(__uuidof(ID3D12InfoQueue), reinterpret_cast<void**>(&queue)))) return;
			D3D12_INFO_QUEUE_FILTER filter{};
			D3D12_MESSAGE_CATEGORY category = D3D12_MESSAGE_CATEGORY_STATE_CREATION;
			D3D12_MESSAGE_SEVERITY severity[] = { D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR };
			D3D12_MESSAGE_ID ignored = static_cast<D3D12_MESSAGE_ID>(1319);
			filter.AllowList.NumCategories = 1;
			filter.AllowList.pCategoryList = &category;
			filter.AllowList.NumSeverities = 2;
			filter.AllowList.pSeverityList = severity;
			filter.DenyList.NumIDs = 1;
			filter.DenyList.pIDList = &ignored;
			queue->ClearStoredMessages();
			queue->SetMessageCountLimit(4096);
			queue->AddStorageFilterEntries(&filter);
			queue->Release();
			g_debugQueueConfigured = true;
		}

		template <class T>
		void put(void* p, size_t at, T value)
		{
			memcpy(static_cast<uint8_t*>(p) + at, &value, sizeof(value));
		}

		template <class T>
		T get_impl(const void* p, size_t at)
		{
			T value{};
			memcpy(&value, static_cast<const uint8_t*>(p) + at, sizeof(value));
			return value;
		}

		Bytes hex(const std::string& text)
		{
			if (text.size() % 2) throw std::runtime_error("odd hex size");
			auto digit = [](char c) -> unsigned
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				throw std::runtime_error("invalid hex digit");
			};
			Bytes result(text.size() / 2);
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = uint8_t(digit(text[2 * i]) * 16 + digit(text[2 * i + 1]));
			return result;
		}

		void parse_impl(std::string_view data)
		{
			const auto doc = nlohmann::json::parse(data);
			if (doc.at("version") != 1 || doc.at("sets") != 169 || doc.at("techniques").size() != 1047)
				throw std::runtime_error("unexpected frontend conversion inventory");
			for (const auto& item : doc.at("shaders").items())
			{
				auto s = std::make_unique<Shader>();
				s->name = item.value().at("name").get<std::string>();
				s->program = hex(item.value().at("program").get<std::string>());
				if (s->program.size() > UINT32_MAX ||
					(!s->program.empty() && (s->program.size() < 32 || memcmp(s->program.data(), "DXBC", 4))))
					throw std::runtime_error("invalid shader container");
				put(s->native.data(), 0, s->name.c_str());
				put(s->native.data(), 24, s->program.empty() ? nullptr : s->program.data());
				put(s->native.data(), 32, uint32_t(s->program.size()));
				g_shaders.emplace(item.key(), std::move(s));
			}
			for (const auto& row : doc.at("techniques"))
			{
				Technique t;
				auto header = hex(row.at("header").get<std::string>());
				if (header.size() != t.header.size()) throw std::runtime_error("invalid technique layout");
				memcpy(t.header.data(), header.data(), header.size());
				for (auto at : kPointers)
					if (get_impl<uint64_t>(header.data(), at)) throw std::runtime_error("serialized pointer in conversion");
				t.sourceArguments = hex(row.at("source_arguments").get<std::string>());
				t.arguments = hex(row.at("arguments").get<std::string>());
				t.state = hex(row.at("state").get<std::string>());
				t.argumentMap = hex(row.at("argument_map").get<std::string>());
				t.pipeline = hex(row.at("pipeline").get<std::string>());
				const size_t args = 6 * size_t(header[120] + header[121] + header[122] + header[123]);
				if (t.arguments.size() != args || t.sourceArguments.size() != args || t.argumentMap.size() != 5 * size_t(header[131]) ||
					t.pipeline.size() != 16 * size_t(header[14]) * header[131] || (!t.state.empty() && t.state.size() != 16))
					throw std::runtime_error("invalid technique child counts");
				for (size_t stage = 0; stage < 4; ++stage)
				{
					if (!row.at("source_shaders")[stage].is_null())
						t.sourceShaders[stage] = row.at("source_shaders")[stage].get<std::string>();
					if (!row.at("shaders")[stage].is_null())
						t.shaders[stage] = g_shaders.at(row.at("shaders")[stage].get<std::string>()).get();
					if (t.sourceShaders[stage].empty() != (t.shaders[stage] == nullptr))
						throw std::runtime_error("changed shader stage presence");
				}
				auto key = row.at("parent").get<std::string>() + "\n" + row.at("name").get<std::string>();
				if (!g_techniques.emplace(key, std::move(t)).second) throw std::runtime_error("duplicate technique");
			}
		}

		bool equal_data(const void* p, const Bytes& expected)
		{
			if (expected.empty()) return p == nullptr;
			Bytes actual(expected.size());
			return safemem::ReadBytes(p, actual.data(), actual.size()) && actual == expected;
		}

		// Returns 0 for unrelated/already-converted data, 1 for conversion, -1 for
		// a known scene record that failed its complete layout/binding identity guard.
		int convert(void* technique, const char* parent)
		{
			char owner[512]{}, name[256]{};
			std::array<uint8_t, 184> header{};
			if (!safemem::ReadString(parent, owner, sizeof(owner)) || !safemem::ReadBytes(technique, header.data(), header.size()) ||
				!safemem::ReadString(get_impl<const char*>(header.data(), 0), name, sizeof(name)))
				return 0;
			auto it = g_techniques.find(std::string(owner) + "\n" + name);
			if (it == g_techniques.end()) return 0;
			const auto& t = it->second;
			// Queue can be called again on a resident technique. Never reset live PSOs.
			if (get_impl<const void*>(header.data(), 176) == t.arguments.data() && !t.arguments.empty()) return 0;
			auto normalized = header;
			for (auto at : kPointers)
				put(normalized.data(), at, uint64_t(0));
			if (normalized != t.header || !equal_data(get_impl<const void*>(header.data(), 176), t.sourceArguments) ||
				!equal_data(get_impl<const void*>(header.data(), 80), t.state) ||
				!equal_data(get_impl<const void*>(header.data(), 136), t.argumentMap))
				return -1;
			for (size_t stage = 0; stage < 4; ++stage)
			{
				const auto ptr = get_impl<const uint8_t*>(header.data(), 88 + 8 * stage);
				if (!ptr)
				{
					if (!t.sourceShaders[stage].empty()) return -1;
					continue;
				}
				// Native packed aliases may resolve through a previously loaded
				// technique's shader field. That field can already contain our exact
				// replacement. Accept only this pass's own expected converted object.
				if (t.shaders[stage] && ptr == t.shaders[stage]->native.data()) continue;
				const char* shaderName = nullptr;
				char shader[128]{};
				if (!safemem::ReadBytes(ptr, &shaderName, sizeof(shaderName)) || !safemem::ReadString(shaderName, shader, sizeof(shader)) ||
					shader != t.sourceShaders[stage])
					return -1;
			}
			auto pipeline = std::make_unique<Bytes>(t.pipeline);
			auto pipelinePtr = pipeline->empty() ? nullptr : pipeline->data();
			g_pipelineStorage.emplace_back(std::move(pipeline));
			put(technique, 40, pipelinePtr);
			put(technique, 176, t.arguments.empty() ? nullptr : t.arguments.data());
			for (size_t stage = 0; stage < 4; ++stage)
				put(technique, 88 + 8 * stage, t.shaders[stage] ? t.shaders[stage]->native.data() : nullptr);
			return 1;
		}

		void trace_technique(void* technique, const char* parent, const char* phase, int result)
		{
			std::array<uint8_t, 184> header{};
			char name[256]{}, owner[512]{};
			if (!safemem::ReadBytes(technique, header.data(), header.size()) ||
				!safemem::ReadString(get_impl<const char*>(header.data(), 0), name, sizeof(name)) || !strstr(name, "8217b301321e325c"))
				return;
			safemem::ReadString(parent, owner, sizeof(owner));
			LOG_INFO("FrontendTrace120", "%s result=%d technique=%p name=%s parent=%s", phase, result, technique, name, owner);
			for (auto offset : { 88, 112 })
			{
				std::array<uint8_t, 40> shader{};
				char shaderName[128]{};
				if (!safemem::ReadBytes(get_impl<const void*>(header.data(), offset), shader.data(), shader.size())) continue;
				safemem::ReadString(get_impl<const char*>(shader.data(), 0), shaderName, sizeof(shaderName));
				std::array<uint8_t, 20> code{};
				safemem::ReadBytes(get_impl<const void*>(shader.data(), 24), code.data(), code.size());
				LOG_INFO("FrontendTrace120", "stage=%d name=%s program=%p bytes=%u checksum=%08X%08X%08X%08X", offset, shaderName,
					get_impl<const void*>(shader.data(), 24), get_impl<unsigned>(shader.data(), 32), get_impl<unsigned>(code.data(), 4),
					get_impl<unsigned>(code.data(), 8), get_impl<unsigned>(code.data(), 12), get_impl<unsigned>(code.data(), 16));
			}
		}

		void __fastcall compile(void* technique, const char* parent, int zone)
		{
			trace_technique(technique, parent, "compile", zone);
			g_compileOriginal.load()(technique, parent, zone);
		}

		void __fastcall queue_impl(void* technique, const char* parent)
		{
			{
				std::lock_guard<std::mutex> lock(g_lock);
				configure_debug_queue();
				const int result = convert(technique, parent);
				if (g_debugEnabled) trace_technique(technique, parent, "queue", result);
				if (result > 0)
				{
					if (!g_converted && g_debugEnabled)
					{
						g_debugQueueConfigured = false;
						configure_debug_queue();
					}
					++g_converted;
					if (g_converted == 1 || g_converted % 100 == 0)
						LOG_INFO("FrontendMaterials120", "converted scene technique programs/bindings before native PSO creation: %u",
							g_converted);
				}
				else if (result < 0 && ++g_rejected <= 12)
				{
					char owner[512]{};
					safemem::ReadString(parent, owner, sizeof(owner));
					LOG_ERR("FrontendMaterials120", "scene technique identity mismatch: %s", owner);
				}
			}
			g_original.load(std::memory_order_acquire)(technique, parent);
		}
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (g_original.load()) return hook::Status::Installed;
		g_base = moduleBase;
		wchar_t debug[8]{};
		if (GetEnvironmentVariableW(L"MAGMA_FRONTEND_D3D_DEBUG", debug, 8) && debug[0] == L'1')
		{
			HMODULE library = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			using GetDebugFn = HRESULT(WINAPI*)(REFIID, void**);
			auto getDebug = library ? reinterpret_cast<GetDebugFn>(GetProcAddress(library, "D3D12GetDebugInterface")) : nullptr;
			ID3D12Debug* layer = nullptr;
			if (getDebug && SUCCEEDED(getDebug(__uuidof(ID3D12Debug), reinterpret_cast<void**>(&layer))))
			{
				layer->EnableDebugLayer();
				layer->Release();
				g_debugEnabled = true;
				LOG_INFO("FrontendMaterials120", "D3D12 diagnostic layer enabled for this test process");
			}
			else
				LOG_WARN("FrontendMaterials120", "D3D12 diagnostic layer unavailable");
		}
		try
		{
			if (g_techniques.empty()) parse_impl(embedded_assets::Find("data/frontend116_materials.json"));
		}
		catch (const std::exception& error)
		{
			LOG_ERR("FrontendMaterials120", "conversion package rejected: %s", error.what());
			return hook::Status::Failed;
		}
		static constexpr uint8_t expected[] = { 0x48, 0x83, 0xEC, 0x48, 0x4C, 0x63, 0x0D, 0x25, 0xBE, 0xB1, 0x01, 0xB8, 0x00, 0x80, 0x00,
			0x00 };
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(moduleBase + 0xECE7E0), &queue_impl, expected, sizeof(expected), g_original);
		if (g_debugEnabled)
		{
			static constexpr uint8_t compileBytes[] = { 0x40, 0x53, 0x56, 0x57, 0xB8, 0x50, 0x33, 0x00, 0x00, 0xE8, 0x12, 0x1A, 0x30,
				0x01 };
			batch.Add(reinterpret_cast<void*>(moduleBase + 0xECB4D0), &compile, compileBytes, sizeof(compileBytes), g_compileOriginal);
		}
		return batch.Commit();
	}

	void ReportPipelineFailure()
	{
		if (!g_debugEnabled || g_reported.exchange(true)) return;
		ID3D12Device* device = nullptr;
		// Replay ECB D51 reads this exact global for CreateGraphicsPipelineState.
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x10C79C20), &device, sizeof(device)) || !device) return;
		ID3D12InfoQueue* queue = nullptr;
		if (FAILED(device->QueryInterface(__uuidof(ID3D12InfoQueue), reinterpret_cast<void**>(&queue)))) return;
		const UINT64 count = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
		unsigned emitted = 0;
		for (UINT64 remaining = count; remaining && emitted < 48;)
		{
			const UINT64 i = --remaining;
			SIZE_T size = 0;
			if (FAILED(queue->GetMessage(i, nullptr, &size)) || size > 1024 * 1024) continue;
			Bytes storage(size);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			if (SUCCEEDED(queue->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
			{
				LOG_ERR("FrontendD3D12", "id=%u %s", unsigned(message->ID), message->pDescription);
				++emitted;
			}
		}
		LOG_INFO("FrontendD3D12", "stored messages=%llu reported errors=%u", count, emitted);
		queue->Release();
	}
}
