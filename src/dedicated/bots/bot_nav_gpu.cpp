#include "dedicated/bots/bot_nav_gpu.h"
#include "dedicated/bots/bot_nav_graph.h"
#include "dedicated/bots/bot_nav_compute.h"
#include "dedicated/headless/d3d.h"

#include "common/console.h"

#include "thirdparty/json.hpp"

#include <windows.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <thread>
#include <algorithm>
namespace botnav
{
	namespace
	{
		constexpr unsigned MaxNodes = 131072, MaxEdges = 1048576;
		std::atomic<int> mode{ 0 }; // 0=off, 1=shadow, 2=use completed verified GPU results
		std::atomic<DWORD> owner{ 0 };
		thread_local unsigned depth = 0;

		struct Ready
		{
			Request request;
			std::vector<uint32_t> visited;
		};

		bool SameRequest(const Request& a, const Request& b)
		{
			return a.source == b.source && a.filter == b.filter;
		}

		struct Runtime
		{
			std::mutex mutex;
			std::condition_variable changed;
			std::shared_ptr<const Graph> current;
			std::deque<Request> pending, inProgress;
			std::deque<std::shared_ptr<const Ready>> ready;
			std::atomic<bool> failed{ false };
			std::atomic<uint64_t> completed{ 0 }, batches{ 0 }, memory{ 0 }, stale{ 0 };
			std::atomic<double> kernelMs{ 0 }, totalMs{ 0 };

			Runtime()
			{
				std::thread(
					[this]
					{
						Worker();
					})
					.detach();
			}

			// This proxy lives for the game process. The worker owns all GPU resources;
			// native threads never wait on a GPU fence or pass native pointers to it.
			void Worker()
			{
				try
				{
					std::unique_ptr<Compute> gpu;
					for (;;)
					{
						std::shared_ptr<const Graph> graph;
						std::vector<Request> work;
						{
							std::unique_lock lock(mutex);
							changed.wait(lock,
								[&]
								{
									return !pending.empty();
								});
							graph = current;
							while (!pending.empty() && work.size() < 16)
							{
								work.push_back(pending.front());
								pending.pop_front();
							}
						}
						if (!gpu)
						{
							gpu = std::make_unique<Compute>(&headless::d3d::DispatchCompute);
							OP_INFO("Bot GPU nav: hardware compute ready | %s", gpu->Adapter().c_str());
						}
						const auto result = gpu->Run(graph, work);
						std::vector<std::shared_ptr<const Ready>> results;
						for (unsigned i = 0; i < work.size(); ++i)
						{
							// Admit a GPU vector only after a full independent CPU BFS
							// comparison, not just one target or a matching checksum.
							if (result.visited[i] != CpuReachable(*graph, work[i].source, work[i].filter))
								throw std::runtime_error("GPU/CPU full reachability vector mismatch");
							results.push_back(std::make_shared<Ready>(Ready{ work[i], result.visited[i] }));
						}
						++batches;
						completed.fetch_add(work.size());
						memory.store(result.localMemory);
						kernelMs.fetch_add(result.kernelMs);
						totalMs.fetch_add(result.totalMs);
						{
							std::lock_guard lock(mutex);
							if (current != graph)
							{
								stale.fetch_add(work.size());
								continue;
							}
							for (const auto& r : results)
							{
								std::erase_if(inProgress,
									[&](const auto& item)
									{
										return SameRequest(item, r->request);
									});
								ready.push_back(r);
								if (ready.size() > 128) ready.pop_front();
							}
						}
					}
				}
				catch (const std::exception& e)
				{
					failed.store(true);
					mode.store(0);
					OP_WARN("Bot GPU nav disabled; native CPU retained | %s", e.what());
				}
			}

			std::shared_ptr<const Ready> Resolve(std::shared_ptr<const Graph> graph, const Request& request)
			{
				std::lock_guard lock(mutex);
				// Exact semantic graph equality: all topology, destination masks and
				// edge rules. Visited epochs/pointers are excluded because they do not
				// change reachability. A changed graph invalidates queued and ready work.
				if (!current || *current != *graph)
				{
					current = graph;
					pending.clear();
					inProgress.clear();
					ready.clear();
				}
				for (const auto& item : ready)
					if (SameRequest(item->request, request)) return item;
				if (!failed.load() && inProgress.size() < 64 &&
					std::none_of(inProgress.begin(), inProgress.end(),
						[&](const auto& r)
						{
							return SameRequest(r, request);
						}))
				{
					pending.push_back(request);
					inProgress.push_back(request);
					changed.notify_one();
				}
				return {};
			}

			bool HasReadyRequest(const Request& request)
			{
				std::lock_guard lock(mutex);
				return std::any_of(ready.begin(), ready.end(),
					[&](const auto& r)
					{
						return SameRequest(r->request, request);
					});
			}
		};

		Runtime& Backend()
		{
			static auto* runtime = new Runtime;
			return *runtime;
		}

		struct Query
		{
			uintptr_t context = 0, nativeGraph = 0, nativeFilter = 0;
			unsigned source = 0;
			Filter filter;
			bool pending = false;
			std::shared_ptr<Graph> graph;
			std::shared_ptr<const Ready> gpu;
			std::vector<uint32_t> cpu;
		};

		thread_local Query query;
		std::shared_ptr<Graph> lastGraph;
		Filter lastFilter;
		unsigned lastSource = 0;
		std::mutex snapshotMutex;
		std::atomic<uint64_t> captures{ 0 }, answers{ 0 }, gpuChecks{ 0 }, gpuServed{ 0 }, mismatches{ 0 }, rejected{ 0 }, onCaptures{ 0 },
			seenContexts{ 0 };
		std::atomic<uint64_t> nativeFastPaths{ 0 }, warmSkips{ 0 };
		std::atomic<double> captureMs{ 0 }, onCaptureMs{ 0 };
		ULONGLONG nextCapture = 0; // native query-owner thread only

		// This copy runs on the calling native navigation thread. No native pointer is
		// retained by a worker. SEH protects the experimental reader; no game writes.
		bool ReadFilter(uintptr_t spec, Filter& filter)
		{
			__try
			{
				filter = { *reinterpret_cast<uint32_t*>(spec), *reinterpret_cast<uint32_t*>(spec + 4),
					*reinterpret_cast<uint32_t*>(spec + 8), *reinterpret_cast<uint32_t*>(spec + 16) };
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool NeedsNativeTraversal(uintptr_t context, int target)
		{
			__try
			{
				// Replay1FF3640 already answers visited nodes and exhausted queues in
				// constant time. Avoid copying a whole graph for these native fast paths.
				const auto queue = *reinterpret_cast<uintptr_t*>(context + 16);
				if (!queue || target < 0 || target == 0x3ffff) return false;
				if (*reinterpret_cast<uintptr_t*>(queue + 24) == *reinterpret_cast<uintptr_t*>(queue + 32)) return false;
				const auto nodes = **reinterpret_cast<uintptr_t***>(context + 8);
				return *reinterpret_cast<uint32_t*>(nodes[target] + 8) != **reinterpret_cast<uint32_t**>(context);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool Dimensions(uintptr_t graph, unsigned& nodes, unsigned& edges)
		{
			__try
			{
				const int n = *reinterpret_cast<int*>(graph + 64);
				const auto list = *reinterpret_cast<uintptr_t**>(graph + 56);
				if (n <= 0 || n > MaxNodes || !list) return false;
				nodes = n;
				edges = 0;
				for (unsigned i = 0; i < nodes; ++i)
					if (list[i])
					{
						const int count = *reinterpret_cast<int*>(list[i] + 40);
						if (count < 0 || unsigned(count) > MaxEdges - edges) return false;
						edges += count;
					}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool CopyGraph(uintptr_t graph, Node* nodes, unsigned n, Edge* edges, unsigned e, uintptr_t spec, Filter* filter)
		{
			__try
			{
				if (*reinterpret_cast<unsigned*>(graph + 64) != n) return false;
				const auto list = *reinterpret_cast<uintptr_t**>(graph + 56);
				if (!list) return false;
				filter->allBlocked = *reinterpret_cast<uint32_t*>(spec);
				filter->blocked = *reinterpret_cast<uint32_t*>(spec + 4);
				filter->allowed = *reinterpret_cast<uint32_t*>(spec + 8);
				filter->links = *reinterpret_cast<uint32_t*>(spec + 16);
				unsigned at = 0;
				for (unsigned i = 0; i < n; ++i)
				{
					nodes[i] = { at, 0, 0, 0 };
					const auto native = list[i];
					if (!native) continue;
					if (*reinterpret_cast<unsigned*>(native + 12) != i) return false;
					const unsigned count = *reinterpret_cast<unsigned*>(native + 40);
					if (count > e - at) return false;
					nodes[i] = { at, count, *reinterpret_cast<uint32_t*>(native + 20), *reinterpret_cast<uint32_t*>(native + 24) };
					const auto links = *reinterpret_cast<uintptr_t*>(native + 32);
					for (unsigned j = 0; j < count; ++j)
					{
						const auto link = links + 16ull * j, to = *reinterpret_cast<uintptr_t*>(link);
						if (!to) return false;
						const auto id = *reinterpret_cast<unsigned*>(to + 12);
						if (id >= n || list[id] != to) return false;
						edges[at++] = { id, *reinterpret_cast<uint8_t*>(link + 8), *reinterpret_cast<uint32_t*>(link + 12), 0 };
					}
				}
				return at == e;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void Dump()
		{
			std::lock_guard lock(snapshotMutex);
			if (!lastGraph)
			{
				OP_WARN("Bot GPU nav: no captured graph");
				return;
			}
			nlohmann::json j = { { "source", lastSource },
				{ "filter", { lastFilter.allBlocked, lastFilter.blocked, lastFilter.allowed, lastFilter.links } },
				{ "native_answer_checks", answers.load() }, { "native_mismatches", mismatches.load() } };
			j["nodes"] = nlohmann::json::array();
			j["edges"] = nlohmann::json::array();
			for (const auto& n : lastGraph->nodes)
				j["nodes"].push_back({ n.first, n.count, n.blocked, n.allowed });
			for (const auto& e : lastGraph->edges)
				j["edges"].push_back({ e.target, e.ordinary, e.mask });
			std::ofstream f("bot-nav-native.json");
			f << j.dump();
			OP_INFO("Bot GPU nav: native snapshot saved to bot-nav-native.json | nodes=%u edges=%u", unsigned(lastGraph->nodes.size()),
				unsigned(lastGraph->edges.size()));
		}
	}

	void BeginQuery()
	{
		++depth;
	}

	void EndQuery()
	{
		if (depth && --depth == 0) query = {};
	}

	void ContextCreated(uintptr_t context, uintptr_t graph, unsigned source, uintptr_t filter)
	{
		if (!depth || !mode.load()) return;
		++seenContexts;
		DWORD unset = 0;
		owner.compare_exchange_strong(unset, GetCurrentThreadId());
		if (GetCurrentThreadId() != owner.load()) return;
		query = {};
		query.context = context;
		query.nativeGraph = graph;
		query.nativeFilter = filter;
		query.source = source;
		query.pending = true;
	}

	void Capture()
	{
		query.pending = false;
		const int activeMode = mode.load();
		if (!activeMode) return;
		Filter f;
		if (!ReadFilter(query.nativeFilter, f))
		{
			++rejected;
			return;
		}
		// A cache miss cannot save any current native work. Warm misses at a bounded
		// rate; every hit still verifies the full graph before using its GPU answer.
		if (activeMode == 1 || !Backend().HasReadyRequest(Request{ query.source, f }))
		{
			if (GetTickCount64() < nextCapture)
			{
				++warmSkips;
				return;
			}
			nextCapture = GetTickCount64() + 250;
		}
		unsigned n = 0, e = 0;
		const auto graph = query.nativeGraph, filter = query.nativeFilter;
		const auto source = query.source;
		LARGE_INTEGER start{}, finish{}, hz{};
		QueryPerformanceFrequency(&hz);
		QueryPerformanceCounter(&start);
		if (!Dimensions(graph, n, e) || source >= n)
		{
			++rejected;
			return;
		}
		auto snapshot = std::make_shared<Graph>();
		snapshot->nodes.resize(n);
		snapshot->edges.resize(e);
		if (!CopyGraph(graph, snapshot->nodes.data(), n, snapshot->edges.data(), e, filter, &f))
		{
			++rejected;
			return;
		}
		query.filter = f;
		query.graph = snapshot;
		if (activeMode == 1) query.cpu = CpuReachable(*snapshot, source, f);
		query.gpu = Backend().Resolve(snapshot, Request{ source, f });
		{
			std::lock_guard lock(snapshotMutex);
			lastGraph = snapshot;
			lastSource = source;
			lastFilter = f;
		}
		++captures;
		QueryPerformanceCounter(&finish);
		const double elapsed = (finish.QuadPart - start.QuadPart) * 1000.0 / hz.QuadPart;
		captureMs.fetch_add(elapsed);
		if (activeMode == 2)
		{
			++onCaptures;
			onCaptureMs.fetch_add(elapsed);
		}
	}

	bool Reachable(uintptr_t context, int target, NativeReachable native)
	{
		if (query.context == context && query.pending)
		{
			if (mode.load() == 2 && !NeedsNativeTraversal(context, target))
			{
				++nativeFastPaths;
				return native(context, target);
			}
			try
			{
				Capture();
			}
			catch (const std::exception& e)
			{
				// Allocation/thread-creation failures in this optional backend must
				// not unwind through the game's native navigation caller.
				mode.store(0);
				query = {};
				OP_WARN("Bot GPU capture disabled; native CPU retained | %s", e.what());
			}
		}
		const bool hasGpu = query.context == context && query.gpu && target >= 0 && unsigned(target) < query.gpu->visited.size();
		const bool gpuAnswer = hasGpu && query.gpu->visited[target] != 0;
		const int activeMode = mode.load();
		if (activeMode == 2 && hasGpu && ((gpuServed.fetch_add(1) + 1) % 256) != 0) return gpuAnswer;
		const bool answer = native(context, target);
		if (query.context == context && target >= 0 && unsigned(target) < query.cpu.size())
		{
			++answers;
			if (answer != (query.cpu[target] != 0))
			{
				++mismatches;
				mode.store(0);
				query.context = 0;
				OP_WARN("Bot GPU nav: native snapshot mismatch; disabled | source=%u target=%d", query.source, target);
			}
		}
		if (hasGpu && activeMode)
		{
			++gpuChecks;
			if (answer != gpuAnswer)
			{
				++mismatches;
				mode.store(0);
				query.context = 0;
				OP_WARN("Bot GPU nav: native/GPU answer mismatch; disabled | target=%d", target);
			}
		}
		return answer;
	}

	void Command(const char* option)
	{
		if (!strcmp(option, "off"))
			mode.store(0);
		else if (!strcmp(option, "shadow"))
		{
			if (Backend().failed.load())
			{
				OP_WARN("Bot GPU nav backend failed; restart required before retry");
				return;
			}
			mode.store(1);
		}
		else if (!strcmp(option, "on"))
		{
			if (mismatches.load() || Backend().failed.load() || gpuChecks.load() < 1024)
			{
				OP_WARN("Bot GPU nav needs 1024 native/GPU shadow checks with no mismatches before on");
				return;
			}
			mode.store(2);
		}
		else if (!strcmp(option, "dump"))
			Dump();
		else if (option[0] && strcmp(option, "status"))
		{
			OP_WARN("Usage: bot_gpu_nav <off|shadow|on|status|dump>");
			return;
		}
		std::lock_guard lock(snapshotMutex);
		OP_INFO(
			"Bot GPU nav | mode=%d owner=%lu contexts=%llu captures=%llu native_checks=%llu gpu_checks=%llu gpu_served=%llu mismatch=%llu rejected=%llu mean_capture=%.3f ms on_capture=%.3f ms nodes=%u edges=%u",
			mode.load(), owner.load(), seenContexts.load(), captures.load(), answers.load(), gpuChecks.load(), gpuServed.load(),
			mismatches.load(), rejected.load(), captures.load() ? captureMs.load() / captures.load() : 0,
			onCaptures.load() ? onCaptureMs.load() / onCaptures.load() : 0, lastGraph ? unsigned(lastGraph->nodes.size()) : 0,
			lastGraph ? unsigned(lastGraph->edges.size()) : 0);
		auto& b = Backend();
		OP_INFO(
			"Bot GPU compute | completed=%llu batches=%llu stale=%llu failed=%d mean_kernel=%.3f ms mean_total=%.3f ms local_memory=%llu bytes",
			b.completed.load(), b.batches.load(), b.stale.load(), b.failed.load(),
			b.batches.load() ? b.kernelMs.load() / b.batches.load() : 0, b.batches.load() ? b.totalMs.load() / b.batches.load() : 0,
			b.memory.load());
		OP_INFO("Bot GPU admission | native_fast_paths=%llu rate_limited_misses=%llu", nativeFastPaths.load(), warmSkips.load());
	}
}
