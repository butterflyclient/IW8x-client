#include "plugin/sdk/abi.h"
_Static_assert(sizeof(iw8x_entity) == 16, "C entity layout");
_Static_assert(sizeof(iw8x_value) == 24, "C value layout");
_Static_assert(sizeof(iw8x_weapon) == 12, "C opaque weapon layout");
_Static_assert(sizeof(iw8x_weapon_snapshot) == 80, "C owned Weapon snapshot layout");
_Static_assert(sizeof(iw8x_player) == 24, "C player POD layout");
_Static_assert(offsetof(iw8x_player, client_num) == 0 && offsetof(iw8x_player, generation) == 8 &&
               offsetof(iw8x_player, xuid) == 16, "C player field offsets");
_Static_assert(sizeof(iw8x_event) == 88, "C extended event layout");
_Static_assert(offsetof(iw8x_event, type) == 0 && offsetof(iw8x_event, epoch) == 8 &&
               offsetof(iw8x_event, entity) == 16 && offsetof(iw8x_event, player) == 32,
               "C original 32-byte event prefix preserved");
_Static_assert(offsetof(iw8x_event, other_player) == 56 && offsetof(iw8x_event, reason) == 80 &&
               offsetof(iw8x_event, flags) == 84, "C appended event fields");
_Static_assert(offsetof(iw8x_plugin_host, register_native) == 24 && offsetof(iw8x_plugin_host, subscribe) == 96 &&
               offsetof(iw8x_plugin_host, hook_function) == 176 && offsetof(iw8x_plugin_host, get_dvar_string) == 184,
               "C existing host function offsets preserved");
_Static_assert(offsetof(iw8x_plugin_host, player_valid) == 192, "C player API appended after existing host table");
_Static_assert(offsetof(iw8x_plugin_host, tell) == 248 && offsetof(iw8x_plugin_host, call_builtin) == 256 &&
               offsetof(iw8x_plugin_host, get_dvar) == 264 && offsetof(iw8x_plugin_host, register_script) == 272 &&
               offsetof(iw8x_plugin_host, call_script) == 280 &&
               offsetof(iw8x_plugin_host, register_script_name) == 288 &&
               offsetof(iw8x_plugin_host, call_script_on) == 296 && sizeof(iw8x_plugin_host) == 304,
               "C invocation/dvar API appended after original 256-byte host");
_Static_assert(IW8X_SCRIPTS_BEGIN_LOAD == 1 && IW8X_MAP_RESTART == 6 && IW8X_PLAYER_CONNECTED == 7 &&
               IW8X_SERVER_STOPPING == 12, "C existing lifecycle IDs preserved");
int sdk_c_version(void) { return IW8X_PLUGIN_API_VERSION; }
