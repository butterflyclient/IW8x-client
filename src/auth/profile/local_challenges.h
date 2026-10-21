#pragma once
#include "auth/profile/challenge_events.h"
#include "auth/profile/progression_notices.h"

#include <functional>
namespace local_challenges
{
	// Game-thread policy expiry is independent of DDL/catalog readiness.
	void ProjectUnlock();
	bool Initialize(int initialXP, const std::map<std::string, int>& legacyWeapons);
	void Reset();
	int WeaponXP(const std::string& weapon);
	uint32_t UTC();
	int BattleXP();
	bool Premium();
	int BattleCP();

	struct CommerceView
	{
		bool ready = false, pending = false;
		int balance = 0;
		std::string status, product;
	};

	CommerceView CommerceStatus();
	std::pair<std::string, int> CurrencyPack(int index);
	bool Purchase(const std::string& kind, const std::string& product, int quotedPrice);
	int TokenQuantity(int item);
	uint32_t TokenExpiry(const char* kind);
	void Receive(const challenges::Event& event);
	void Pump(const std::function<int(const std::string&)>& weaponRank);
	int XP();
	bool Owns(int item);
	bool UnlockEnabled();
	bool SetUnlock(bool enabled);

	struct UnlockView
	{
		bool ready = false, pending = false, enabled = false;
		uint64_t revision = 0;
		std::string status;
	};

	UnlockView UnlockStatus();
	std::vector<std::string> AfterAction(const std::string& operation, const std::string& argument);
	int Progress(int id); // -1 means no implemented rule, retain native behavior
	std::string Assignments(const char* kind); // id:progress pairs separated by commas
	int Mission(int category);
	bool Activate(int id);
	void ObserveRank(const std::string& key, const std::string& event, int rank, const std::string& weapon,
		const std::map<int, std::vector<int>>& grants);
	progression::Notice NextNotice(bool challenge);
	bool AckNotice(uint64_t sequence);
}
