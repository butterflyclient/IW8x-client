#pragma once
/* IW8x plugin ABI v1. Win64, C calling convention, default 8-byte packing.
 * All input views are borrowed for the duration of the call. No allocation or
 * exception may cross this boundary. A successful registration transfers data
 * to the host; destroy runs exactly once before unloading the owning DLL. */
#include <stdint.h>
#include <stddef.h>
#pragma pack(push, 8)
#define IW8X_PLUGIN_API_VERSION 1u
#ifdef __cplusplus
extern "C"
{
#endif
	typedef uint32_t iw8x_plugin_id;
	typedef uint64_t iw8x_registration;
	typedef int32_t iw8x_native_handle;
	typedef uint32_t iw8x_status;

	enum
	{
		IW8X_OK,
		IW8X_INVALID_ARGUMENT,
		IW8X_UNSUPPORTED,
		IW8X_DUPLICATE,
		IW8X_NOT_FOUND,
		IW8X_WRONG_THREAD,
		IW8X_STALE,
		IW8X_STOPPING,
		IW8X_BUSY,
		IW8X_TYPE_MISMATCH,
		IW8X_CALLBACK_ERROR,
		IW8X_LIMIT,
		IW8X_VERSION_MISMATCH,
		IW8X_INTERNAL_ERROR
	};

	typedef struct iw8x_string
	{
		const char* data;
		uint32_t size;
	} iw8x_string;

	typedef struct iw8x_vector
	{
		float x, y, z;
	} iw8x_vector;

	/* Legacy, incorrect vector-shaped Weapon representation. Retained only for
	 * ABI layout compatibility; live calls reject IW8X_WEAPON as unsupported. */
	typedef struct iw8x_weapon
	{
		uint32_t words[3];
	} iw8x_weapon;

	/* Owned Replay 1.20.4 Weapon snapshot: 60 native bytes, reserved bytes, alternate-mode byte, zero
	 * padding, and host generations. No VM object IDs or engine pointers escape.
	 * Treat all fields as opaque; obtain snapshots through GSC calls. */
	typedef struct iw8x_weapon_snapshot
	{
		uint32_t words[16];
		uint64_t map_epoch;
		uint64_t script_generation;
	} iw8x_weapon_snapshot;

	typedef struct iw8x_entity
	{
		uint32_t index;
		uint32_t reserved;
		uint64_t generation;
	} iw8x_entity;

	typedef struct iw8x_player
	{
		uint32_t client_num;
		uint32_t reserved;
		uint64_t generation;
		uint64_t xuid;
	} iw8x_player;

	enum
	{
		IW8X_UNDEFINED,
		IW8X_INTEGER,
		IW8X_NUMBER,
		IW8X_BOOLEAN,
		IW8X_STRING,
		IW8X_VECTOR,
		IW8X_ENTITY,
		IW8X_ARRAY,
		IW8X_STRUCT,
		IW8X_WEAPON,
		IW8X_WEAPON_SNAPSHOT
	};
	typedef struct iw8x_value iw8x_value;
	typedef struct iw8x_field iw8x_field;

	struct iw8x_value
	{
		uint32_t type;
		uint32_t count;

		union
		{
			int32_t integer;
			float number;
			uint32_t boolean;
			iw8x_string string;
			iw8x_vector vector;
			iw8x_weapon weapon;
			const iw8x_weapon_snapshot* weapon_snapshot;
			iw8x_entity entity;
			const iw8x_value* elements;
			const iw8x_field* fields;
		} as;
	};

	struct iw8x_field
	{
		iw8x_string name;
		iw8x_value value;
	};

	enum
	{
		IW8X_CAP_FUNCTIONS = 1ull << 0,
		IW8X_CAP_METHODS = 1ull << 1,
		IW8X_CAP_ENTITIES = 1ull << 2,
		IW8X_CAP_NOTIFY = 1ull << 3,
		IW8X_CAP_FIELDS = 1ull << 4,
		IW8X_CAP_GAME_TASKS = 1ull << 5,
		IW8X_CAP_ASYNC = 1ull << 6,
		IW8X_CAP_COMMANDS = 1ull << 7,
		IW8X_CAP_INTERCEPTION = 1ull << 8,
		IW8X_CAP_DVARS = 1ull << 9,
		IW8X_CAP_PLAYERS = 1ull << 10,
		IW8X_CAP_CHAT = 1ull << 11,
		IW8X_CAP_GSC_CALL = 1ull << 12,
		IW8X_CAP_RICH_VALUES = 1ull << 13,
		IW8X_CAP_TYPED_DVARS = 1ull << 14,
		IW8X_CAP_SCRIPT_CALL = 1ull << 15
	};

	enum
	{
		IW8X_GAME_INITIALIZED,
		IW8X_SCRIPTS_BEGIN_LOAD,
		IW8X_SCRIPTS_LOADED,
		IW8X_SCRIPTS_SHUTDOWN,
		IW8X_MAP_STARTED,
		IW8X_MAP_SHUTDOWN,
		IW8X_MAP_RESTART,
		IW8X_PLAYER_CONNECTED,
		IW8X_PLAYER_DISCONNECTED,
		IW8X_PLAYER_SPAWNED,
		IW8X_PLAYER_KILLED,
		IW8X_SERVER_STARTED,
		IW8X_SERVER_STOPPING,
		/* Append-only ABI v1 lifecycle additions. */
		IW8X_MAP_STARTING,
		IW8X_PLAYER_CONNECTING,
		IW8X_PLAYER_DISCONNECTING,
		IW8X_PLAYER_DIED,
		IW8X_SERVER_STARTING,
		IW8X_SERVER_STOPPED,
		IW8X_EVENT_COUNT,
		IW8X_MAP_READY = IW8X_MAP_STARTED,
		IW8X_PLAYER_SPAWN = IW8X_PLAYER_SPAWNED,
		IW8X_PLAYER_DEATH = IW8X_PLAYER_DIED,
		IW8X_PLAYER_KILL = IW8X_PLAYER_KILLED,
		IW8X_SERVER_READY = IW8X_SERVER_STARTED
	};

	enum
	{
		IW8X_SCOPE_PLUGIN,
		IW8X_SCOPE_MAP
	};

	enum
	{
		IW8X_EVENT_HAS_REASON = 1u << 0
	};

	/* The first 32 bytes are the original ABI v1 event layout. New fields are
	 * appended so already-built v1 plugins continue to read the same prefix. */
	typedef struct iw8x_event
	{
		uint32_t type;
		uint32_t reserved;
		uint64_t epoch;
		iw8x_entity entity;
		iw8x_player player;
		iw8x_player other_player;
		uint32_t reason;
		uint32_t flags;
	} iw8x_event;

	/* call is an opaque invocation ID, never an engine pointer. */
	typedef void (*iw8x_native_callback)(void* data, uint64_t call, iw8x_entity self);
	typedef void (*iw8x_event_callback)(void* data, const iw8x_event* event);
	typedef void (*iw8x_task_callback)(void* data);
	typedef void (*iw8x_destroy_callback)(void* data);
	typedef void (*iw8x_command_callback)(void* data, const iw8x_string* args, uint32_t count);
	typedef void (*iw8x_resource_callback)(void* data, void* resource);
	typedef void (*iw8x_value_callback)(void* data, const iw8x_value* value);
	typedef void (*iw8x_players_callback)(void* data, const iw8x_player* players, uint32_t count);

	typedef struct iw8x_plugin_host
	{
		uint32_t version;
		uint32_t size;
		uint64_t (*capabilities)(void);
		uint64_t (*supported_events)(void);
		iw8x_status (*register_native)(
			iw8x_plugin_id, iw8x_string name, uint32_t method, iw8x_native_callback, void*, iw8x_destroy_callback);
		iw8x_status (*argument_count)(uint64_t call, uint32_t* count);
		iw8x_status (*argument)(uint64_t call, uint32_t index, iw8x_value_callback, void*);
		iw8x_status (*return_value)(uint64_t call, const iw8x_value*);
		iw8x_status (*error)(uint64_t call, iw8x_string message);
		iw8x_status (*entity_valid)(iw8x_entity);
		iw8x_status (*get_field)(iw8x_entity, iw8x_string, iw8x_value_callback, void*);
		iw8x_status (*set_field)(iw8x_entity, iw8x_string, const iw8x_value*);
		/* zero generation denotes level only for notify, never an entity. */
		iw8x_status (*notify)(iw8x_entity, iw8x_string, const iw8x_value*, uint32_t);
		iw8x_status (*subscribe)(iw8x_plugin_id, uint32_t event, iw8x_event_callback, void*, iw8x_destroy_callback, iw8x_registration*);
		iw8x_status (*schedule)(
			iw8x_plugin_id, uint32_t delay_ms, uint32_t scope, iw8x_task_callback, void*, iw8x_destroy_callback, iw8x_registration*);
		/* work runs on host worker; complete on server; destroy after complete/cancel. */
		iw8x_status (*async)(iw8x_plugin_id, uint32_t scope, iw8x_task_callback work, iw8x_task_callback complete, void*,
			iw8x_destroy_callback, iw8x_registration*);
		/* Only pending tasks can be canceled; running tasks return BUSY. */
		iw8x_status (*cancel_task)(iw8x_plugin_id, iw8x_registration);
		iw8x_status (*create_handle)(
			iw8x_plugin_id, uint64_t type, uint32_t scope, void* resource, iw8x_destroy_callback, iw8x_native_handle*);
		/* Borrow resource only inside callback. release returns BUSY during a borrow. */
		iw8x_status (*with_handle)(iw8x_plugin_id, iw8x_native_handle, uint64_t type, iw8x_resource_callback, void*);
		iw8x_status (*release_handle)(iw8x_plugin_id, iw8x_native_handle, uint64_t type);
		iw8x_status (*add_command)(iw8x_plugin_id, iw8x_string, iw8x_command_callback, void*, iw8x_destroy_callback);
		void (*log)(iw8x_plugin_id, iw8x_string);
		/* SDK exception adapters call this; current callback is marked failed. */
		void (*callback_error)(iw8x_plugin_id, iw8x_string);
		/* Reserved logical script interception. v1 returns UNSUPPORTED and takes
		 * no ownership; script dispatch/lifetime has not yet been verified. */
		iw8x_status (*hook_function)(iw8x_plugin_id, iw8x_string script, iw8x_string function, iw8x_native_callback, void*,
			iw8x_destroy_callback, iw8x_registration*);
		/* Append-only ABI v1 extension. Read-only string dvar access is restricted
		 * to the verified game/server-thread scope. The returned value is borrowed
		 * only for the duration of receive; SDK wrappers copy it immediately. */
		iw8x_status (*get_dvar_string)(iw8x_string name, iw8x_value_callback receive, void* data);
		/* Player handles are host-owned identities. They never expose SvClientMP,
		 * gclient_s, gentity_s or any other retail pointer. */
		iw8x_status (*player_valid)(iw8x_player player);
		iw8x_status (*player_name)(iw8x_player player, iw8x_value_callback receive, void* data);
		iw8x_status (*player_entity)(iw8x_player player, iw8x_entity* entity);
		iw8x_status (*player_client_num)(iw8x_player player, uint32_t* client_num);
		iw8x_status (*player_xuid)(iw8x_player player, uint64_t* xuid);
		iw8x_status (*players)(iw8x_players_callback receive, void* data);
		/* Stock MW text-chat transport. These calls must run in verified game/server
		 * scope. Messages are copied before returning and never expose SvClientMP. */
		iw8x_status (*broadcast)(iw8x_string message);
		iw8x_status (*tell)(iw8x_player player, iw8x_string message);
		/* Append-only v1. Values are recursively borrowed only during receive.
		 * No engine reference or allocation crosses the ABI. */
		iw8x_status (*call_builtin)(iw8x_plugin_id, uint32_t method, iw8x_entity self, iw8x_string name, const iw8x_value*, uint32_t count,
			iw8x_value_callback receive, void* data);
		/* Readable Replay names or known retail names. Requested type is STRING,
		 * BOOLEAN, INTEGER or NUMBER. Unknown/unregistered names return NOT_FOUND. */
		iw8x_status (*get_dvar)(iw8x_plugin_id, iw8x_string name, uint32_t type, iw8x_value_callback receive, void* data);
		/* Append-only v1 stock-script invocation. Register before script loading;
		 * the host resolves the export while Scr_EndLoadScripts lookup state still
		 * exists, then the registration can be called during that map generation. */
		iw8x_status (*register_script)(
			iw8x_plugin_id, iw8x_string script, uint32_t function_token, uint32_t result_type, iw8x_registration* registration);
		iw8x_status (*call_script)(
			iw8x_plugin_id, iw8x_registration registration, const iw8x_value*, uint32_t count, iw8x_value_callback receive, void* data);
		/* Preferred append-only v1 entry: IW8 canonical function name lookup.
		 * Register before Scr_EndLoadScripts. Equivalent owner/path/token/type
		 * requests reuse a registration; export handles refresh on script reload.
		 * Unknown names/exports: NOT_FOUND; unresolved/retired generation: STALE. */
		iw8x_status (*register_script_name)(
			iw8x_plugin_id, iw8x_string script, iw8x_string function, uint32_t result_type, iw8x_registration* registration);
		/* Invoke a registered export with a generation-checked entity as GSC self. */
		iw8x_status (*call_script_on)(iw8x_plugin_id, iw8x_registration, iw8x_entity self, const iw8x_value*, uint32_t count,
			iw8x_value_callback receive, void* data);
	} iw8x_plugin_host;

	typedef uint32_t (*iw8x_plugin_load)(const iw8x_plugin_host*, iw8x_plugin_id);
	typedef void (*iw8x_plugin_unload)(void);
	/* Export uint32_t IW8X_PluginLoad(...): nonzero success. Optional
	 * void IW8X_PluginUnload(void): called after all host callbacks quiesce,
	 * before host-owned userdata destruction. Join plugin-created threads here. */
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
