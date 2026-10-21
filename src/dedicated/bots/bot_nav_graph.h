#pragma once
#include <cstdint>
#include <vector>
namespace botnav
{
	struct Node
	{
		uint32_t first = 0, count = 0, blocked = 0, allowed = 0;
		bool operator==(const Node&) const = default;
	};

	struct Edge
	{
		uint32_t target = 0, ordinary = 0, mask = 0, pad = 0;
		bool operator==(const Edge&) const = default;
	};

	struct Filter
	{
		uint32_t allBlocked = 0, blocked = 0, allowed = 0, links = 0;
		bool operator==(const Filter&) const = default;
	};

	struct Graph
	{
		std::vector<Node> nodes;
		std::vector<Edge> edges;
		bool operator==(const Graph&) const = default;
	};

	// Exact destination/edge predicate from Replay 1FF4480. The source is always
	// seeded as visited, even if its own masks would reject entry from another node.
	inline bool Traversable(const Node& to, const Edge& edge, const Filter& f)
	{
		const auto blocked = to.blocked & f.blocked;
		return !(f.allBlocked ? blocked == f.blocked : blocked != 0) && (!to.allowed || (to.allowed & f.allowed)) &&
			(edge.ordinary || (edge.mask & f.links));
	}

	inline std::vector<uint32_t> CpuReachable(const Graph& graph, unsigned source, const Filter& filter)
	{
		std::vector<uint32_t> visited(graph.nodes.size()), queue;
		if (source >= visited.size()) return visited;
		queue.reserve(visited.size());
		queue.push_back(source);
		visited[source] = 1;
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const auto& node = graph.nodes[queue[head]];
			for (uint32_t i = 0; i < node.count; ++i)
			{
				const auto& edge = graph.edges[node.first + i];
				if (!visited[edge.target] && Traversable(graph.nodes[edge.target], edge, filter))
				{
					visited[edge.target] = 1;
					queue.push_back(edge.target);
				}
			}
		}
		return visited;
	}
}
