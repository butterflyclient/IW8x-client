#include "diagnostics/island/island_renderer_adapter.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::fprintf(stderr, "Island renderer adapter test failed: %s\n", message);
			std::exit(EXIT_FAILURE);
		}
	}
}

int main()
{
	using namespace island_renderer_adapter;
	const draw_context valid{"mp_sm_island_1", true, true};
	set_depth_preview(false);
	set_enabled(false);
	require(!enabled(), "default disabled");
	require(!classify(valid).eligible, "disabled rejects candidate");
	set_enabled(true);
	require(enabled(), "opt-in enables");
	require(classify(valid).eligible, "valid scoped, paired Island candidate is eligible");
	require(!classify(valid).translate_depth, "no unproven automatic depth translation");
	require(!classify({nullptr, true, true}).eligible, "null world rejected");
	require(!classify({"mp_sm_island", true, true}).eligible, "prefix map rejected");
	require(!classify({"mp_sm_island_1_extra", true, true}).eligible, "suffix map rejected");
	require(!classify({"mp_don4", true, true}).eligible, "native map rejected");
	require(!classify({"mp_sm_island_1", false, true}).eligible, "native material rejected");
	require(!classify({"mp_sm_island_1", true, false}).eligible, "unpaired material rejected");
	require(!classify({"mp_sm_island_1", false, false}).eligible, "non imported and unpaired rejected");
	set_depth_preview(true);
	require(classify({"mp_sm_island_1", true, false}).translate_depth,
		"explicit Island import preview may bypass an absent prepass");
	require(!classify({"mp_shipment", true, false}).translate_depth,
		"native Shipment never receives depth preview");
	require(!classify({"mp_sm_island_1", false, false}).translate_depth,
		"native Island materials cannot receive preview");
	set_enabled(false);
	require(!classify({"mp_sm_island_1", true, false}).translate_depth,
		"turning off global opt-in disables experimental depth mode");
	set_depth_preview(false);
	set_enabled(true);
	for (const auto* map : {"mp_shipment", "mp_donetsk", "mp_escape2", "mp_kstenod"})
		require(!classify({map, true, true}).eligible, "unrelated map cannot inherit Island policy");
	set_enabled(false);
	require(!classify(valid).eligible, "disable restores unmodified behavior");
	std::puts("Island-only renderer adapter gates passed (Release assertions independent)");
}
