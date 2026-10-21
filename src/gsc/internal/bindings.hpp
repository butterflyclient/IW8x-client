#pragma once
#include "plugin/sdk/value.hpp"

#include <span>
namespace iw8x::gsc::internal
{
	// Private engine boundary. Implementations are owned by IW8x, never plugins.
	// Capability bits are published only after guards and all dependent hooks pass.
	struct engine_bindings
	{
		uint64_t capabilities{};
		iw8x_status (*valid)(entity_ref) = nullptr;
		iw8x_status (*get_field)(entity_ref, std::string_view, value&) = nullptr;
		iw8x_status (*set_field)(entity_ref, std::string_view, const value&) = nullptr;
		iw8x_status (*notify)(entity_ref, std::string_view, std::span<const value>) = nullptr;
		iw8x_status (*reserve_native)(bool method, std::string_view plugin, std::string_view name) = nullptr;
		void (*invalidate_entities)() = nullptr;
		iw8x_status (*validate_return)(const value&) = nullptr;
		iw8x_status (*get_dvar_string)(std::string_view, value&) = nullptr;
		iw8x_status (*player_valid)(iw8x_player) = nullptr;
		iw8x_status (*player_name)(iw8x_player, value&) = nullptr;
		iw8x_status (*player_entity)(iw8x_player, iw8x_entity&) = nullptr;
		iw8x_status (*broadcast_chat)(std::string_view) = nullptr;
		iw8x_status (*tell_chat)(iw8x_player, std::string_view) = nullptr;
		iw8x_status (*call_builtin)(bool, entity_ref, std::string_view, std::span<const value>, value&) = nullptr;
		iw8x_status (*call_script)(uint32_t, uint32_t, std::span<const value>, value&) = nullptr;
		iw8x_status (*get_dvar)(std::string_view, uint32_t, value&) = nullptr;
		uint32_t (*resolve_script)(void*, const char*, uint32_t) = nullptr;
		iw8x_status (*call_script_on)(entity_ref, uint32_t, uint32_t, std::span<const value>, value&) = nullptr;
	};

	const engine_bindings& bindings();
	void configure_bindings(engine_bindings bindings); // Startup only, before plugin loading.
	bool in_game_scope() noexcept;

	class game_scope
	{
	public:
		explicit game_scope(bool allow_vm = true);
		~game_scope();
		game_scope(const game_scope&) = delete;

	private:
		bool entered_{}, previous_allowed_{}, owns_vm_{};
	};
}
