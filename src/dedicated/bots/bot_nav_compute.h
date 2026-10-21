#pragma once
#include "dedicated/bots/bot_nav_graph.h"
#include <memory>
#include <string>
namespace botnav
{
	struct Request
	{
		uint32_t source = 0;
		Filter filter;
		uint32_t pad[3]{};
	};

	static_assert(sizeof(Request) == 32);

	struct GpuResult
	{
		std::vector<std::vector<uint32_t>> visited;
		double kernelMs = 0, totalMs = 0;
		uint64_t localMemory = 0;
	};

	using DispatchCompute = bool (*)(void*, unsigned, unsigned, unsigned);

	class Compute
	{
	public:
		explicit Compute(DispatchCompute dispatch = nullptr, bool debug = false, bool software = false);
		~Compute();
		GpuResult Run(std::shared_ptr<const Graph> graph, const std::vector<Request>& requests);
		std::string Adapter() const;
		unsigned DebugErrors() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
