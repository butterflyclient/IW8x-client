#pragma once
#include "network/social/runtime/types.hpp"
namespace social_runtime::detail
{
	void release(uintptr_t object);

	uintptr_t user_list_address(int type);

	uintptr_t* list_fetch(uintptr_t self, uintptr_t* out, const char* name, const char* page, uintptr_t response);

	uintptr_t* list_create(uintptr_t self, uintptr_t* out, const char* name, uintptr_t response);

	uintptr_t* list_mutation(uintptr_t* out, int type, uintptr_t rows, unsigned count, uintptr_t response, bool remove);

	uintptr_t* list_add(
		uintptr_t self, uintptr_t* out, const char* name, uintptr_t rows, unsigned count, bool trim, unsigned maximum, uintptr_t response);

	uintptr_t* list_remove(uintptr_t self, uintptr_t* out, const char* name, uintptr_t rows, unsigned count, uintptr_t response);

	bool apply_user_list(uintptr_t response, const Json& body);

	bool prepare_user_lists(hook::Batch& batch);

	int availability(uint64_t id);

	int pending_availability(uint64_t id);

	char set_availability(int value);

	void apply_availability(int value);

	uintptr_t* get_friends(uintptr_t self, uintptr_t* out, uintptr_t response, const char* page);

	uintptr_t username_bearer(uintptr_t request, const char* value);

	uintptr_t* search_username(uintptr_t self, uintptr_t* out, uintptr_t request, uintptr_t response);

	uintptr_t* subscribe(uintptr_t self, uintptr_t* out, uintptr_t accounts, unsigned count, uintptr_t response);

	uintptr_t* message(uintptr_t self, uintptr_t* out, uint64_t target, const void* data, unsigned n);

	uintptr_t* requests_impl(uintptr_t self, uintptr_t* out, uintptr_t response, bool sent);

	uintptr_t* add_friend(uintptr_t self, uintptr_t* out, uintptr_t accounts, unsigned count, uintptr_t response);

	uintptr_t* remove_friend(uintptr_t self, uintptr_t* out, uintptr_t account, uintptr_t response);

	uintptr_t invite_impl(uintptr_t self, unsigned controller, uintptr_t ids, uintptr_t platforms, int count);

	uintptr_t atomic_join(uintptr_t self);

	uintptr_t invite_network();

	uintptr_t invite_requirements(unsigned controller);

	uintptr_t invite_auth(uintptr_t self);

	bool fence(int controller, uint64_t mask);

	unsigned flags(int controller);

	bool apply_friends(uintptr_t response, const Json& body, bool requests = false);

	bool apply_subscriptions(uintptr_t response, const Json& body);

	bool apply_username(uintptr_t response, const Json& body);

	void complete_impl(const Result& r);

	void apply_sync(const Json& body);
}
