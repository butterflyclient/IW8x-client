// Production bootstrap with instrumented subsystem gates; no game process.
#include "bootstrap/native_plugins.cpp"

#include <iostream>
#include <vector>
#include <stdexcept>
namespace
{
	bool dedicated_mode = true, bap_enabled = true, bap_ready{};
	hook::Status runtime_status = hook::Status::NotReady, loader_status = hook::Status::NotReady;
	std::vector<std::string> calls;
	unsigned checks{}, worker_calls{}, unload_calls{};
	std::filesystem::path table_root, discovered_root;

	void expect(bool pass, const char* what)
	{
		++checks;
		if (!pass) throw std::runtime_error(what);
	}
}

namespace console
{
	void Print(const char*, ...)
	{
	}

	void OperatorInfo(const char*, ...)
	{
	}

	void OperatorWarn(const char*, ...)
	{
	}
}

namespace dedicated
{
	bool Enabled()
	{
		return dedicated_mode;
	}
}

namespace bap
{
	bool Enabled()
	{
		return bap_enabled;
	}

	bool Ready()
	{
		return bap_ready;
	}
}

namespace game
{
	uintptr_t base() noexcept
	{
		return 0x140000000;
	}
}

namespace iw8x::gsc::internal
{
	void set_log_sink(log_sink)
	{
		calls.emplace_back("log");
	}

	void initialize_tables(const std::filesystem::path& root)
	{
		table_root = root;
		calls.emplace_back("tables");
	}

	hook::Status install_runtime()
	{
		calls.emplace_back("runtime");
		return runtime_status;
	}
}

namespace iw8x::gsc
{
	void pump_worker() noexcept
	{
		++worker_calls;
	}
}

namespace gsc
{
	hook::Status Install(uintptr_t base)
	{
		expect(base == game::base(), "bootstrap keeps Replay image base");
		calls.emplace_back("loader");
		return loader_status;
	}

	namespace link_diagnostics
	{
		void Install(uintptr_t)
		{
			calls.emplace_back("diagnostics");
		}
	}
}

namespace iw8x::plugins
{
	void discover(const std::filesystem::path& root)
	{
		discovered_root = root;
		calls.emplace_back("discover");
	}

	std::vector<loaded_plugin> loaded()
	{
		return {};
	}

	iw8x_status unload(iw8x_plugin_id id)
	{
		expect(id == 7, "management unload parses exact owner ID");
		++unload_calls;
		return IW8X_OK;
	}
}

int main()
{
	try
	{
		native_plugins::PumpWorker();
		expect(!worker_calls, "worker remains disarmed before native install");
		expect(native_plugins::Install() == hook::Status::NotReady && calls.empty(), "dedicated bootstrap waits for BAP readiness");
		bap_ready = true;
		expect(native_plugins::Install() == hook::Status::NotReady && calls == std::vector<std::string>{ "log", "tables", "runtime" },
			"plugin discovery cannot precede runtime guards");
		calls.clear();
		runtime_status = hook::Status::Installed;
		expect(native_plugins::Install() == hook::Status::NotReady && calls == std::vector<std::string>{ "runtime", "loader" },
			"plugin discovery cannot precede compiled loader");
		calls.clear();
		loader_status = hook::Status::Installed;
		expect(native_plugins::Install() == hook::Status::Installed &&
				calls == std::vector<std::string>{ "runtime", "loader", "diagnostics", "discover" },
			"runtime/loader install before discovery; optional diagnostics do not gate support");
		expect(
			table_root == discovered_root && table_root.filename() == L"plugins", "bindings and discovery share DLL-relative plugin root");
		calls.clear();
		expect(native_plugins::Install() == hook::Status::Installed && calls.empty(), "successful bootstrap is idempotent");
		native_plugins::PumpWorker();
		expect(worker_calls == 1, "installed bootstrap owns async pump");
		expect(command::dispatch("iw8x_plugin_unload") && command::dispatch("iw8x_plugin_unload junk") &&
				command::dispatch("iw8x_plugin_unload 7junk") && !unload_calls,
			"missing/invalid unload ID never calls host");
		expect(command::dispatch("iw8x_plugin_unload 7") && unload_calls == 1, "management command reaches installed host");
		command::remove("iw8x_plugins");
		command::remove("iw8x_plugin_unload");
		// The common host also serves a client hosting an MP game. BAP is a
		// dedicated prerequisite; it must not block the client bootstrap.
		native_plugins::installed.store(false);
		native_plugins::prepared = false;
		dedicated_mode = false;
		bap_ready = false;
		calls.clear();
		expect(native_plugins::Install() == hook::Status::Installed &&
				calls == std::vector<std::string>{ "log", "tables", "runtime", "loader", "diagnostics", "discover" },
			"client bootstrap installs the same host without a dedicated BAP gate");
		command::remove("iw8x_plugins");
		command::remove("iw8x_plugin_unload");
		std::cout << "Replay bootstrap: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAILED: " << e.what() << '\n';
		return 1;
	}
}
