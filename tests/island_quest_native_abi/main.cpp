#include "../../src/gsc/compiled/island_quest_native_abi.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static std::vector<std::uint8_t> load(const char* file)
{
	std::ifstream stream(file, std::ios::binary);
	if (!stream) throw std::runtime_error("Source or staged fixture is missing");
	return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

int main(int argc, char** argv)
{
	if (argc != 3) return 2;
	using namespace gsc::island_quest_native_abi;
	auto source = load(argv[1]);
	const auto candidate = load(argv[2]);
	assert(source.size() == source_length);
	assert(fingerprint(source) == source_fingerprint);
	assert(candidate.size() == source_length);
	assert(fingerprint(candidate) == replay_fingerprint);
	assert(translate_private_compiler_copy(source));
	assert(source == candidate);
	// A second application must not corrupt already-patched compiler bytecode.
	auto replay = candidate;
	assert(!translate_private_compiler_copy(replay));
	assert(replay == candidate);
	// Any changed opcode, ID, other byte or length must fail without mutating.
	for (const auto& site : source_calls)
	{
		auto tampered = load(argv[1]);
		tampered[site.opcode_offset] ^= 1;
		auto expected = tampered;
		assert(!translate_private_compiler_copy(tampered) && tampered == expected);
	}
	auto altered = load(argv[1]);
	altered[0] ^= 1;
	const auto expected = altered;
	assert(!translate_private_compiler_copy(altered) && altered == expected);
	assert(!translate_private_compiler_copy(std::span<std::uint8_t>(altered.data(), altered.size() - 1)));
	std::cout << "PASS: original source and Replay 7-callsite companion ABI, exact full-file fingerprints, fail-closed mutation, no double patch\n";
}
