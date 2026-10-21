#include "client/frontend/native_commerce.h"
#include "auth/profile/local_challenges.h"
#include "auth/profile/local_progression.h"
#include "network/commerce/custom_mod_backend.h"
#include "network/backend/frontend_backend.h"
#include "network/commerce/store_backend.h"
#include "common/embedded_assets.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "engine/game.h"
#include "auth/profile/xp_tokens.h"
#include "thirdparty/json.hpp"
#include <atomic>
#include <algorithm>
#include <deque>
#include <cmath>
#include <vector>
// Service providers for the SHIPPED Store/BattlePass/Fence Lua. No menu
// constructors, purchase buttons, popup classes or presentation timers here.

namespace native_commerce
{
	namespace
	{
		using Json = nlohmann::json;
		uintptr_t base = 0;
		void* currentVM = nullptr;
		int publishedBalance = -1;
		std::vector<int> passThresholds;
		using LuaFn = int (*)(void*);

		struct Registration
		{
			const char* name;
			LuaFn callback;
		};

		template <class F>
		F native(uintptr_t rva)
		{
			return reinterpret_cast<F>(base + rva);
		}

		using BalanceFn = int (*)(uintptr_t, int, int);
		std::atomic<BalanceFn> originalBalance{ nullptr };
		Json offers = Json::object(), activations = Json::object();

		struct Event
		{
			void* vm;
			std::string name;
			Json fields;
		};

		std::deque<Event> events;
		std::string pendingProduct, pendingKind;
		void* purchaseVM = nullptr;

		int number(void* vm, int index)
		{
			if (!native<int (*)(void*, int)>(0x20839E0)(vm, index)) return -1;
			const auto value = native<double (*)(void*, int)>(0x2084A10)(vm, index);
			return std::isfinite(value) && value >= 0 && value <= 100000000 && std::floor(value) == value ? int(value) : -1;
		}

		std::string string_impl(void* vm, int index)
		{
			size_t size = 0;
			const auto* p = native<const char* (*)(void*, int, size_t*)>(0x2084950)(vm, index, &size);
			return p && size <= 128 ? std::string(p, size) : std::string{};
		}

		int push(void* vm, const Json& value)
		{
			if (value.is_boolean())
				native<void (*)(void*, int)>(0x2083E80)(vm, value.get<bool>());
			else if (value.is_number())
				native<void (*)(void*, double)>(0x2084100)(vm, value.get<double>());
			else if (value.is_string())
				native<void (*)(void*, const char*)>(0x2084120)(vm, value.get_ref<const std::string&>().c_str());
			else if (value.is_object() || value.is_array())
			{
				native<void (*)(void*, int, int)>(0x20833B0)(vm, 0, 0);
				if (value.is_object())
					for (const auto& [key, item] : value.items())
					{
						push(vm, item);
						native<void (*)(void*, int, const char*)>(0x2084510)(vm, -2, key.c_str());
					}
				else
				{
					int i = 0;
					for (const auto& item : value)
					{
						push(vm, ++i);
						push(vm, item);
						native<void (*)(void*, int)>(0x2084720)(vm, -3);
					}
				}
			}
			else
				native<void (*)(void*, int)>(0x2083E80)(vm, 0);
			return 1;
		}

		int enum_impl(void* vm, const char* table, const char* key)
		{
			const auto top = native<int (*)(void*)>(0x20838B0)(vm);
			native<void (*)(void*, int, const char*)>(0x20836E0)(vm, -10002, table);
			native<void (*)(void*, int, const char*)>(0x20836E0)(vm, -1, key);
			const int value = number(vm, -1);
			native<void (*)(void*, int)>(0x2084790)(vm, top);
			return value;
		}

		bool ready_impl()
		{
			return frontend_backend::Authenticated() && store_backend::Get().ready && local_challenges::CommerceStatus().ready;
		}

		void queue(void* vm, const char* name, Json fields = Json::object())
		{
			if (events.size() < 64) events.push_back({ vm, name, std::move(fields) });
		}

		int balance_impl(uintptr_t self, int controller, int currency)
		{
			if (frontend_backend::Required() && controller == 0 && currency == 20) return local_challenges::BattleCP();
			return originalBalance.load()(self, controller, currency);
		}

		int lua_balance(void* vm)
		{
			if (number(vm, 1) == 0 && number(vm, 2) == 20) return push(vm, local_challenges::BattleCP());
			return native<LuaFn>(0x1B32400)(vm);
		}

		void purchase(void* vm, const std::string& kind, const std::string& product, int quote)
		{
			// Repeated input must not cancel the native popup for an active transaction.
			if (!pendingProduct.empty()) return;
			if (!local_challenges::Purchase(kind, product, quote))
			{
				if (kind == "trial")
				{
					queue(vm, "trial_activation_failed", { { "immediate", true } });
					return;
				}
				if (kind == "token")
				{
					queue(vm, "token_activation_failed", { { "controller", 0 }, { "controllerIndex", 0 } });
					return;
				}
				queue(vm, kind == "currency" ? "ingamestore_purchased_product" : "loot_item_transaction_error",
					{ { "controllerIndex", 0 }, { "transactionResult", 3 }, { "errorCode", 5 } });
				return;
			}
			pendingProduct = product;
			pendingKind = kind;
			purchaseVM = vm;
			LOG_INFO("Commerce", "native purchase queued kind=%s product=%s", kind.c_str(), product.c_str());
		}

		int buy_item(void* vm)
		{
			const auto id = std::to_string(number(vm, 2));
			if (number(vm, 1) != 0 || !offers.contains(id))
			{
				queue(vm, "loot_item_transaction_error", { { "errorCode", 8003 } });
				return 0;
			}
			purchase(vm, "bundle", id, offers.at(id).at("price"));
			return 0;
		}

		Json product_impl(void* vm, const std::string& id, int amount)
		{
			return { { "productId", id }, { "image", "mw_cod_points_" + std::to_string(amount) },
				{ "popupImage", "mw_cod_points_" + std::to_string(amount) + "_large" }, { "title", std::to_string(amount) + " CP" },
				{ "contentLong", std::to_string(amount) + " COD Points" }, { "price", "0" }, { "currencyId", 20 },
				{ "status", enum_impl(vm, "InGameStoreProductStatus", "NOT_OWNED") }, { "bytesDownloaded", 0 }, { "bytesTotal", 0 },
				{ "skuType", enum_impl(vm, "InGameStoreProductSkuType", "FIRST_PARTY") } };
		}

		int fetch(void* vm)
		{
			return push(vm, number(vm, 1) == 0 && frontend_backend::Required());
		}

		int catalog_status(void* vm)
		{
			return push(vm, enum_impl(vm, "InGameStoreCatalogStatus", ready_impl() ? "READY" : "FETCHING"));
		}

		int categories(void* vm)
		{
			Json result = Json::array();
			if (number(vm, 1) == 0 && ready_impl())
				result.push_back({ { "categoryId", "codpoints" }, { "title", "COD Points" }, { "image", "mw_store_billboard_cod_points" },
					{ "layout", "" } });
			return push(vm, result);
		}

		int products(void* vm)
		{
			Json result = Json::array();
			if (number(vm, 1) == 0 && ready_impl() && string_impl(vm, 2) == "codpoints")
				for (int i = 0; i < 16; ++i)
				{
					const auto [id, amount] = local_challenges::CurrencyPack(i);
					if (id.empty()) break;
					result.push_back(product_impl(vm, id, amount));
				}
			return push(vm, result);
		}

		int get_product(void* vm)
		{
			if (number(vm, 1) == 0 && ready_impl())
				for (int i = 0; i < 16; ++i)
				{
					const auto [id, amount] = local_challenges::CurrencyPack(i);
					if (id.empty()) break;
					if (id == string_impl(vm, 2)) return push(vm, product_impl(vm, id, amount));
				}
			return 0;
		}

		int select(void* vm)
		{
			if (number(vm, 1) == 0) purchase(vm, "currency", string_impl(vm, 2), 0);
			return 0;
		}

		int token_quantity(void* vm)
		{
			return push(vm, number(vm, 1) == 0 ? local_challenges::TokenQuantity(number(vm, 2)) : 0);
		}

		int activate_token(void* vm)
		{
			const auto* token = xp_tokens::Achievement(number(vm, 2));
			if (number(vm, 1) == 0 && token)
				purchase(vm, "token", std::to_string(token->item), 0);
			else
				queue(vm, "token_activation_failed", { { "controller", 0 }, { "controllerIndex", 0 } });
			return 0;
		}

		int activate_trial(void* vm)
		{
			if (number(vm, 1) == 0 && number(vm, 2) > 0 && number(vm, 3) == xp_tokens::TrialTicket)
				purchase(vm, "trial", "200005", 0);
			else
				queue(vm, "trial_activation_failed", { { "immediate", true } });
			return 0;
		}

		int token_expiries(void* vm)
		{
			// Shipped DoubleXPNotifications expects player, weapon, keys, Battle Pass.
			for (const auto* kind : { "player", "weapon", "keys", "battle" })
				push(vm, number(vm, 1) == 0 ? local_challenges::TokenExpiry(kind) : 0u);
			return 4;
		}

		int pass_xp(void* vm)
		{
			if (number(vm, 1) == 0 && number(vm, 2) == 3) return push(vm, local_challenges::BattleXP());
			return push(vm, 0);
		}

		int pass_owned(void* vm)
		{
			return push(vm, number(vm, 1) == 0 && number(vm, 2) == 3 && local_challenges::Premium());
		}

		int quantity(void* vm)
		{
			const int id = number(vm, 2);
			if (number(vm, 1) != 0) return native<LuaFn>(0x1B39ED0)(vm);
			if (xp_tokens::Counted(id)) return push(vm, local_challenges::TokenQuantity(id));
			// BP consumables in old local snapshots were already redeemed server-side.
			if (id == 432000 || (id >= 430002 && id <= 430008)) return push(vm, 0);
			if (local_challenges::Owns(id)) return push(vm, 1);
			return native<LuaFn>(0x1B39ED0)(vm);
		}

		int pass_activate(void* vm)
		{
			const auto id = std::to_string(number(vm, 3));
			const bool ok = number(vm, 1) == 0 && activations.contains(id) &&
				number(vm, 2) == activations.at(id).at("challenge").get<int>() && local_challenges::CommerceStatus().ready &&
				!local_challenges::CommerceStatus().pending &&
				(id == "432000" ? local_challenges::Premium() : local_challenges::BattleXP() > 0);
			queue(vm, ok ? "battlepass_activated" : "battlepass_activation_failed", { { "immediate", true }, { "controller", 0 } });
			return 0;
		}

		int pass_event(void* vm)
		{
			const auto event = string_impl(vm, 2);
			bool ok = false;
			for (const auto& activation : activations)
				if (activation.at("event") == event) ok = true;
			ok = ok && number(vm, 1) == 0 && local_challenges::CommerceStatus().ready && !local_challenges::CommerceStatus().pending;
			// All grants were saved with the purchase; these native completion stages
			// acknowledge that snapshot and must never create additional awards.
			const auto xp = local_challenges::BattleXP();
			const int tier =
				std::max(0, int(std::upper_bound(passThresholds.begin(), passThresholds.end(), xp) - passThresholds.begin()) - 1);
			queue(vm, ok ? "battlepass_event_complete" : "battlepass_send_event_failed",
				{ { "immediate", true }, { "controller", 0 }, { "xp", xp }, { "rank", tier } });
			return 0;
		}

		int no_fixup(void* vm)
		{
			return push(vm, false);
		}

		int fixup(void* vm)
		{
			queue(vm, "battlepass_fixup_complete", { { "immediate", true } });
			return 0;
		}

		int online_data(void* vm)
		{
			if (number(vm, 1) == 0 && number(vm, 2) == 0 && frontend_backend::Authenticated())
			{
				const auto mods = custom_mod_backend::Status();
				if (local_challenges::XP() < 0 || !local_progression::Get(-1).ready || !mods.ready || !store_backend::Get().ready)
					return push(vm, 2);
			}
			return native<LuaFn>(0x1B11330)(vm);
		}

		void bind(void* vm, const char* name, const Registration* entries)
		{
			const auto top = native<int (*)(void*)>(0x20838B0)(vm);
			native<void (*)(void*, const char*, const Registration*)>(0x20885C0)(vm, name, entries);
			native<void (*)(void*, int)>(0x2084790)(vm, top);
		}
	}

	void Register(void* vm)
	{
		currentVM = vm;
		events.clear();
		publishedBalance = -1;
		const Registration store[] = { { "FetchCatalog", fetch }, { "GetCatalogStatus", catalog_status }, { "GetCategories", categories },
			{ "GetCategoryProducts", products }, { "GetProduct", get_product }, { "SelectProduct", select }, { nullptr, nullptr } };
		const Registration commerce[] = { { "CEDDCGHIGA", lua_balance }, { nullptr, nullptr } };
		const Registration loot[] = { { "HAJHHFGJJ", buy_item }, { "DEBEIFJEEG", quantity }, { nullptr, nullptr } };
		const Registration pass[] = { { "GAICDBJEC", buy_item }, { "CBHBFIJDHG", pass_activate }, { "BHAGDBCJDJ", pass_event },
			{ "BAGCGCIHAF", pass_xp }, { "DEBEIFJEEG", pass_owned }, { "DFIJFJGED", no_fixup }, { "BEBDCFAHB", fixup },
			{ nullptr, nullptr } };
		const Registration fences[] = { { "CIHFIEHIDE", online_data }, { nullptr, nullptr } };
		const Registration progression[] = { { "BIDJFJIHC", token_quantity }, { "DCAHGJEDJJ", activate_token },
			{ "CEHEBBGFGG", token_expiries }, { nullptr, nullptr } };
		bind(vm, "OnlineProgression", progression);
		const Registration trials[] = { { "DCEJEBIAFF", activate_trial }, { nullptr, nullptr } };
		bind(vm, "Trials", trials);
		bind(vm, "InGameStore", store);
		bind(vm, "Commerce", commerce);
		bind(vm, "Loot", loot);
		bind(vm, "Battlepass", pass);
		bind(vm, "Fences", fences);
		LOG_INFO("Commerce", "native Store, COD Points, Battle Pass and profile-fence providers registered");
	}

	void Pump()
	{
		if (!base || !frontend_backend::Required()) return;
		void* vm = nullptr;
		safemem::ReadBytes(reinterpret_cast<void*>(base + game::kLUILuaVMRVA), &vm, sizeof(vm));
		if (!vm || vm != currentVM) return;
		const auto view = local_challenges::CommerceStatus();
		if (view.ready && view.balance != publishedBalance)
		{
			// The shipped CurrencyWidget subscribes to frontEnd.commerce models.
			// Publish through the engine's commerce updater after a real snapshot.
			native<void (*)(int)>(0x1B20F30)(0);
			publishedBalance = view.balance;
		}
		if (!pendingProduct.empty() && view.ready && !view.pending && view.product == pendingProduct && view.status != "pending")
		{
			const bool ok =
				view.status == "credited" || view.status == "purchased" || view.status == "already_owned" || view.status == "activated";
			if (pendingKind == "trial")
				queue(purchaseVM, ok ? "trial_activated" : "trial_activation_failed", { { "immediate", true } });
			else if (pendingKind == "token")
				queue(purchaseVM, ok ? "token_activation_success" : "token_activation_failed",
					{ { "controller", 0 }, { "controllerIndex", 0 } });
			else if (pendingKind == "currency")
				queue(purchaseVM, "ingamestore_purchased_product", { { "controllerIndex", 0 }, { "transactionResult", ok ? 1 : 3 } });
			else
				queue(purchaseVM, ok ? "loot_item_transaction_complete" : "loot_item_transaction_error",
					{ { "lootItemID", std::stoi(pendingProduct) }, { "errorCode", ok ? 0 : 5 } });
			pendingProduct.clear();
			pendingKind.clear();
			purchaseVM = nullptr;
		}
		// Pop before dispatch: a stock event handler may queue the next BP stage.
		const auto count = events.size();
		for (size_t i = 0; i < count && !events.empty(); ++i)
		{
			const auto e = events.front();
			events.pop_front();
			if (e.vm != vm) continue;
			if (native<char (*)(int, const char*, void*)>(0x19BCC30)(0, e.name.c_str(), vm))
			{
				for (const auto& [key, value] : e.fields.items())
				{
					if (value.is_boolean())
						native<void (*)(const char*, bool, void*)>(0x19CE460)(key.c_str(), value.get<bool>(), vm);
					else if (value.is_number_integer())
						native<void (*)(const char*, int64_t, void*)>(0x19CE4F0)(key.c_str(), value.get<int64_t>(), vm);
				}
				native<void (*)(void*)>(0x19BDA00)(vm);
				LOG_INFO("Commerce", "native completion event=%s", e.name.c_str());
			}
		}
	}

	hook::Status Install(uintptr_t image)
	{
		base = image;
		// Trials.DCEJEBIAFF: TU19 one-state callback, exactly three numeric args;
		// original native completion consumes one item then emits trial_activated.
		const unsigned char trialExpected[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20,
			0x48, 0x8b, 0xf1 };
		unsigned char trialActual[sizeof(trialExpected)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x1078890), trialActual, sizeof(trialActual)) ||
			memcmp(trialActual, trialExpected, sizeof(trialActual)))
			return hook::Status::NotReady;
		// ABIs/prologues verified against Replay; research lives in
		// ida-reports/native-commerce-{bindings,providers,completions}.json.
		const std::pair<uintptr_t, const char*> checks[] = { { 0x20833B0, "48895c24084889742410574883ec204c" },
			{ 0x2084510, "48895c2408574883ec204d8bd0488bd9" }, { 0x2084720, "40534883ec20488bd9e832e1ffff4c8b" },
			{ 0x19BCC30, "48896c24104889742418574883ec208b" }, { 0x19BDA00, "40534883ec20488bd1b903000000e82d" },
			{ 0x1B11330, "4889742420574883ec20488bf933f6e8" }, { 0x1B39ED0, "40574883ec4048895c2450488bf94889" },
			{ 0x1B20F30, "48895c241048896c2418488974242089" } };
		for (const auto& [rva, hex] : checks)
		{
			unsigned char bytes[15]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + rva), bytes, 15)) return hook::Status::NotReady;
			for (int i = 0; i < 15; ++i)
			{
				const auto expected = std::stoul(std::string(hex + 2 * i, 2), nullptr, 16);
				if (bytes[i] != expected) return hook::Status::NotReady;
			}
		}
		try
		{
			const auto tables = Json::parse(embedded_assets::Find("data/server_progression_catalog.json")).at("tables");
			for (const auto& row : tables.at("loot/battlepass_season3.csv"))
				passThresholds.push_back(std::stoi(row.at(1).get<std::string>()));
			for (const auto& row : tables.at("loot/bundle_ids.csv"))
				if (row.at(9) == "20" && !row.at(10).get_ref<const std::string&>().empty())
					offers[row.at(0).get<std::string>()] = { { "price", std::stoi(row.at(10).get<std::string>()) } };
			for (const auto& row : tables.at("loot/battlepass_ids.csv"))
			{
				const auto id = row.at(0).get<std::string>();
				if (row.at(16) == "3" || row.at(5) == "skip_tier")
				{
					offers[id] = { { "price", std::stoi(row.at(8).get<std::string>()) } };
					if (row.at(5) != "")
						activations[id] = { { "challenge", std::stoi(row.at(6).get<std::string>()) }, { "event", row.at(5) } };
				}
			}
		}
		catch (...)
		{
			return hook::Status::Failed;
		}
		const unsigned char bytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		return hook::Install(reinterpret_cast<void*>(base + 0x1B32B70), balance_impl, bytes, sizeof(bytes), originalBalance);
	}
}
