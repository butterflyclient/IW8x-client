#pragma once
#include "value.hpp"
#include "dvars.hpp"

#include <array>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
namespace iw8x
{
	using plugin_id = iw8x_plugin_id;
	using plugin_host = iw8x_plugin_host;
	inline constexpr uint32_t api_version = IW8X_PLUGIN_API_VERSION;

	class api_error : public std::runtime_error
	{
	public:
		explicit api_error(iw8x_status code) : std::runtime_error("IW8x API status " + std::to_string(code)), code_(code)
		{
		}

		api_error(iw8x_status code, const std::string& detail)
			: std::runtime_error(detail + " (IW8x API status " + std::to_string(code) + ")"), code_(code)
		{
		}

		iw8x_status code() const noexcept
		{
			return code_;
		}

	private:
		iw8x_status code_;
	};

	inline void check(iw8x_status status)
	{
		if (status != IW8X_OK) throw api_error(status);
	}

	namespace gsc
	{
		inline void check_script(iw8x_status status, const std::string& label)
		{
			if (status == IW8X_OK) return;
			const auto reason = status == IW8X_NOT_FOUND ? "IW8X_NOT_FOUND: unknown function name or missing script export"
				: status == IW8X_STALE ? "IW8X_STALE: export awaits resolution in Scr_EndLoadScripts for this map"
				: status == IW8X_TYPE_MISMATCH ? "IW8X_TYPE_MISMATCH: script return does not match the requested type"
				: "script registration/call failed";
			throw api_error(status, label + ": " + reason);
		}
		enum class hook_result
		{
			continue_original,
			skip_original
		}; // Logical interception is disabled in ABI v1.

		// Receiver catches inside the allocating DLL. No C++ exception crosses the ABI.
		struct value_receiver
		{
			value result;
			std::exception_ptr error;

			static void receive(void* data, const iw8x_value* source) noexcept
			{
				auto& self = *static_cast<value_receiver*>(data);
				try
				{
					self.result = value::copy(*source);
				}
				catch (...)
				{
					self.error = std::current_exception();
				}
			}

			value take()
			{
				if (error) std::rethrow_exception(error);
				return std::move(result);
			}
		};

		class entity
		{
		public:
			entity(const plugin_host& api, entity_ref ref) : api_(&api), ref_(ref)
			{
			}

			entity_ref ref() const noexcept
			{
				return ref_;
			}

			bool valid() const
			{
				return api_->entity_valid(ref_) == IW8X_OK;
			}

			value get_field(std::string_view name) const
			{
				value_receiver out;
				check(api_->get_field(ref_, string_view(name), &value_receiver::receive, &out));
				return out.take();
			}

			void set_field(std::string_view name, const value& v) const
			{
				encoded_value encoded(v);
				check(api_->set_field(ref_, string_view(name), &encoded.view()));
			}

			vector origin() const
			{
				return get_field("origin").as<vector>();
			}

			void notify(std::string_view name, std::initializer_list<value> args = {}) const
			{
				std::vector<encoded_value> encoded;
				encoded.reserve(args.size());
				std::vector<iw8x_value> views;
				views.reserve(args.size());
				for (const auto& arg : args)
					encoded.emplace_back(arg);
				for (const auto& arg : encoded)
					views.push_back(arg.view());
				check(api_->notify(ref_, string_view(name), views.data(), static_cast<uint32_t>(views.size())));
			}

		private:
			const plugin_host* api_;
			entity_ref ref_;
		};

		class context
		{
		public:
			context(const plugin_host& api, uint64_t call) : api_(&api), call_(call)
			{
			}

			uint32_t argument_count() const
			{
				uint32_t n{};
				check(api_->argument_count(call_, &n));
				return n;
			}

			template <class T>
			T argument(uint32_t index)
			{
				// Stable references for string_view last for this context's lifetime.
				auto it = arguments_.find(index);
				if (it == arguments_.end())
				{
					value_receiver out;
					check(api_->argument(call_, index, &value_receiver::receive, &out));
					it = arguments_.emplace(index, out.take()).first;
				}
				if constexpr (std::is_same_v<T, entity>)
					return entity(*api_, it->second.as<entity_ref>());
				else
					return it->second.as<T>();
			}

			void return_value(const value& result)
			{
				encoded_value encoded(result);
				check(api_->return_value(call_, &encoded.view()));
			}

			void return_value(entity result)
			{
				return_value(value(result.ref()));
			}

			void return_undefined()
			{
				return_value(value{});
			}

			[[noreturn]] void error(std::string_view message)
			{
				check(api_->error(call_, string_view(message)));
				throw api_error(IW8X_CALLBACK_ERROR);
			}

		private:
			const plugin_host* api_;
			uint64_t call_;
			std::map<uint32_t, value> arguments_;
		};

		class script_function
		{
		public:
			script_function(const plugin_host& host, plugin_id owner, iw8x_registration registration, std::string label = "script")
				: host_(&host), owner_(owner), registration_(registration), label_(std::move(label))
			{
			}

			iw8x_registration registration() const noexcept
			{
				return registration_;
			}

			template <class... Args>
			value operator()(Args&&... args) const
			{
				return invoke({ value(std::forward<Args>(args))... });
			}

			template <class... Args>
			value on(entity_ref self, Args&&... args) const
			{
				return invoke_on(&self, { value(std::forward<Args>(args))... });
			}

			template <class... Args>
			value on(const entity& self, Args&&... args) const
			{
				return on(self.ref(), std::forward<Args>(args)...);
			}

			value invoke(std::initializer_list<value> args) const
			{
				return invoke_on(nullptr, args);
			}

		private:
			value invoke_on(const entity_ref* self, std::initializer_list<value> args) const
			{
				if (self ? (host_->size < offsetof(plugin_host, call_script_on) + sizeof(host_->call_script_on) || !host_->call_script_on)
					: (host_->size < offsetof(plugin_host, call_script) + sizeof(host_->call_script) || !host_->call_script))
					throw api_error(IW8X_UNSUPPORTED);
				std::vector<encoded_value> encoded;
				encoded.reserve(args.size());
				std::vector<iw8x_value> views;
				views.reserve(args.size());
				for (const auto& arg : args)
					encoded.emplace_back(arg);
				for (const auto& arg : encoded)
					views.push_back(arg.view());
				value_receiver out;
				const auto count = static_cast<uint32_t>(views.size());
				const auto status = self
					? host_->call_script_on(owner_, registration_, *self, views.data(), count, &value_receiver::receive, &out)
					: host_->call_script(owner_, registration_, views.data(), count, &value_receiver::receive, &out);
				check_script(status, label_);
				return out.take();
			}

			const plugin_host* host_;
			plugin_id owner_;
			iw8x_registration registration_{};
			std::string label_;
		};

		class caller
		{
		public:
			caller(const plugin_host& host, plugin_id owner) : host_(&host), owner_(owner)
			{
			}

			template <class... Args>
			value call(std::string_view name, Args&&... args) const
			{
				return invoke(false, {}, name, { value(std::forward<Args>(args))... });
			}

			template <class... Args>
			value method(entity_ref self, std::string_view name, Args&&... args) const
			{
				return invoke(true, self, name, { value(std::forward<Args>(args))... });
			}

			template <class... Args>
			value method(const entity& self, std::string_view name, Args&&... args) const
			{
				return method(self.ref(), name, std::forward<Args>(args)...);
			}

			script_function script(std::string_view path, uint32_t function_token, value_type result_type = value_type::undefined) const
			{
				if (host_->size < offsetof(plugin_host, register_script) + sizeof(host_->register_script) || !host_->register_script)
					throw api_error(IW8X_UNSUPPORTED);
				iw8x_registration registration{};
				const auto label = std::string(path) + "::token " + std::to_string(function_token);
				check_script(
					host_->register_script(owner_, string_view(path), function_token, static_cast<uint32_t>(result_type), &registration),
					label);
				return { *host_, owner_, registration, label };
			}

			// Register at plugin load (or SCRIPTS_BEGIN_LOAD); invocation uses the
			// cached export after Scr_EndLoadScripts. Repeated requests are idempotent.
			script_function script(std::string_view path, std::string_view function, value_type result_type = value_type::undefined) const
			{
				if (host_->size < offsetof(plugin_host, register_script_name) + sizeof(host_->register_script_name) ||
					!host_->register_script_name)
					throw api_error(IW8X_UNSUPPORTED);
				iw8x_registration registration{};
				const auto label = std::string(path) + "::" + std::string(function);
				check_script(host_->register_script_name(
								 owner_, string_view(path), string_view(function), static_cast<uint32_t>(result_type), &registration),
					label);
				return { *host_, owner_, registration, label };
			}

			template <class Result = value, class... Args>
			Result call_script(std::string_view path, std::string_view function, Args&&... args) const
			{
				static_assert(!std::is_reference_v<Result> && !std::is_same_v<Result, std::string_view>,
					"call_script returns owned values; use std::string instead of string_view");
				const auto type = []
				{
					if constexpr (std::is_void_v<Result> || std::is_same_v<Result, value>)
						return value_type::undefined;
					else
						return value(Result{}).type();
				}();
				auto result = script(path, function, type)(std::forward<Args>(args)...);
				if constexpr (!std::is_void_v<Result>) return result.template as<Result>();
			}

			value invoke(bool method, entity_ref self, std::string_view name, std::initializer_list<value> args) const
			{
				if (host_->size < offsetof(plugin_host, call_builtin) + sizeof(host_->call_builtin) || !host_->call_builtin)
					throw api_error(IW8X_UNSUPPORTED);
				std::vector<encoded_value> encoded;
				encoded.reserve(args.size());
				std::vector<iw8x_value> views;
				views.reserve(args.size());
				for (const auto& arg : args)
					encoded.emplace_back(arg);
				for (const auto& arg : encoded)
					views.push_back(arg.view());
				value_receiver out;
				check(host_->call_builtin(owner_, method, self, string_view(name), views.data(), static_cast<uint32_t>(views.size()),
					&value_receiver::receive, &out));
				return out.take();
			}

		private:
			const plugin_host* host_;
			plugin_id owner_;
		};
	}

	class dvar_access
	{
	public:
		dvar_access(const plugin_host& host, plugin_id owner) : host_(&host), owner_(owner)
		{
		}

		std::string get_string(std::string_view name) const
		{
			return read(name, IW8X_STRING).as<std::string>();
		}

		bool get_bool(std::string_view name) const
		{
			return read(name, IW8X_BOOLEAN).as<bool>();
		}

		int32_t get_int(std::string_view name) const
		{
			return read(name, IW8X_INTEGER).as<int32_t>();
		}

		float get_float(std::string_view name) const
		{
			return read(name, IW8X_NUMBER).as<float>();
		}

	private:
		gsc::value read(std::string_view name, uint32_t type) const
		{
			if (host_->size < offsetof(plugin_host, get_dvar) + sizeof(host_->get_dvar) || !host_->get_dvar)
				throw api_error(IW8X_UNSUPPORTED);
			gsc::value_receiver out;
			check(host_->get_dvar(owner_, gsc::string_view(name), type, &gsc::value_receiver::receive, &out));
			return out.take();
		}

		const plugin_host* host_;
		plugin_id owner_;
	};

	class player
	{
	public:
		player(const plugin_host& api, iw8x_player ref) : api_(&api), ref_(ref)
		{
		}

		iw8x_player ref() const noexcept
		{
			return ref_;
		}

		bool valid() const
		{
			return api_->player_valid(ref_) == IW8X_OK;
		}

		uint32_t client_num() const
		{
			uint32_t out{};
			check(api_->player_client_num(ref_, &out));
			return out;
		}

		uint64_t xuid() const
		{
			uint64_t out{};
			check(api_->player_xuid(ref_, &out));
			return out;
		}

		std::string name() const
		{
			gsc::value_receiver out;
			check(api_->player_name(ref_, &gsc::value_receiver::receive, &out));
			return std::string(out.take().as<std::string_view>());
		}

		std::optional<gsc::entity> entity() const
		{
			iw8x_entity out{};
			const auto status = api_->player_entity(ref_, &out);
			if (status == IW8X_NOT_FOUND) return std::nullopt;
			check(status);
			return gsc::entity(*api_, out);
		}

	private:
		const plugin_host* api_;
		iw8x_player ref_{};
	};

	class api
	{
	public:
		api(const plugin_host* host, plugin_id owner) : host_(host), owner_(owner)
		{
			if (!host || host->version != api_version || host->size < offsetof(plugin_host, call_builtin))
				throw api_error(IW8X_VERSION_MISMATCH);
		}

		const plugin_host& raw() const
		{
			return *host_;
		}

		plugin_id owner() const
		{
			return owner_;
		}

		gsc::caller gsc() const
		{
			return { *host_, owner_ };
		}

		dvar_access dvars() const
		{
			return { *host_, owner_ };
		}

		api namespace_(std::string_view name) const
		{
			api out(*this);
			out.prefix_ += std::string(name) + "_";
			return out;
		}

		template <class F>
		void function(std::string_view name, F callback) const
		{
			native<false>(name, std::move(callback));
		}

		template <class F>
		void method(std::string_view name, F callback) const
		{
			native<true>(name, std::move(callback));
		}

		gsc::entity level() const
		{
			return { *host_, {} };
		}

		player player_from(const iw8x_player ref) const
		{
			return { *host_, ref };
		}

		std::vector<player> players() const
		{
			struct receiver
			{
				std::vector<player> result;
				const plugin_host* host;
				std::exception_ptr error;
			} out{ {}, host_, {} };

			check(host_->players(
				[](void* data, const iw8x_player* refs, uint32_t count) noexcept
				{
					auto& out = *static_cast<receiver*>(data);
					try
					{
						for (uint32_t i = 0; i < count; ++i)
							out.result.emplace_back(*out.host, refs[i]);
					}
					catch (...)
					{
						out.error = std::current_exception();
					}
				},
				&out));
			if (out.error) std::rethrow_exception(out.error);
			return std::move(out.result);
		}

		template <class F>
		iw8x_registration subscribe(uint32_t event, F callback) const
		{
			using box = callback_box<F>;
			auto p = std::make_unique<box>(box{ host_, owner_, std::move(callback) });
			iw8x_registration id{};
			check(host_->subscribe(
				owner_, event,
				[](void* p, const iw8x_event* e) noexcept
				{
					auto& b = *static_cast<box*>(p);
					safely(b,
						[&]
						{
							b.callback(*e);
						});
				},
				p.get(), &destroy<box>, &id));
			p.release();
			return id;
		}

		template <class F>
		iw8x_registration game_thread(F callback, uint32_t delay_ms = 0, uint32_t scope = IW8X_SCOPE_MAP) const
		{
			using box = callback_box<F>;
			auto p = std::make_unique<box>(box{ host_, owner_, std::move(callback) });
			iw8x_registration id{};
			check(host_->schedule(
				owner_, delay_ms, scope,
				[](void* p) noexcept
				{
					auto& b = *static_cast<box*>(p);
					safely(b,
						[&]
						{
							b.callback();
						});
				},
				p.get(), &destroy<box>, &id));
			p.release();
			return id;
		}

		// State stays in the plugin's CRT; worker and completion share it. Host
		// owns the box until completion or cancellation and drains work on unload.
		template <class Work, class Complete>
		iw8x_registration worker(Work work, Complete complete, uint32_t scope = IW8X_SCOPE_MAP) const
		{
			using result = std::invoke_result_t<Work>;

			struct box
			{
				const plugin_host* host;
				plugin_id owner;
				Work work;
				Complete complete;
				std::unique_ptr<result> value;
			};

			auto p = std::make_unique<box>(box{ host_, owner_, std::move(work), std::move(complete), {} });
			iw8x_registration id{};
			check(host_->async(
				owner_, scope,
				[](void* p) noexcept
				{
					auto& b = *static_cast<box*>(p);
					safely(b,
						[&]
						{
							b.value = std::make_unique<result>(b.work());
						});
				},
				[](void* p) noexcept
				{
					auto& b = *static_cast<box*>(p);
					safely(b,
						[&]
						{
							b.complete(std::move(*b.value));
						});
				},
				p.get(), &destroy<box>, &id));
			p.release();
			return id;
		}

		template <class F>
		void command(std::string_view name, F callback) const
		{
			using box = callback_box<F>;
			auto p = std::make_unique<box>(box{ host_, owner_, std::move(callback) });
			check(host_->add_command(
				owner_, gsc::string_view(name),
				[](void* p, const iw8x_string* args, uint32_t n) noexcept
				{
					auto& b = *static_cast<box*>(p);
					safely(b,
						[&]
						{
							b.callback(args, n);
						});
				},
				p.get(), &destroy<box>));
			p.release();
		}

		std::string dvar_string(std::string_view name) const
		{
			gsc::value_receiver out;
			check(host_->get_dvar_string(gsc::string_view(name), &gsc::value_receiver::receive, &out));
			const auto value = out.take();
			return std::string(value.as<std::string_view>());
		}

		void broadcast(std::string_view message) const
		{
			check(host_->broadcast(gsc::string_view(message)));
		}

		void tell(const player& target, std::string_view message) const
		{
			check(host_->tell(target.ref(), gsc::string_view(message)));
		}

	private:
		template <class F>
		struct callback_box
		{
			const plugin_host* host;
			plugin_id owner;
			F callback;
		};

		template <class Box, class F>
		static void safely(Box& b, F action) noexcept
		{
			try
			{
				action();
			}
			catch (const std::exception& e)
			{
				b.host->callback_error(b.owner, gsc::string_view(e.what()));
			}
			catch (...)
			{
				b.host->callback_error(b.owner, gsc::string_view("plugin callback threw"));
			}
		}

		template <class T>
		static void destroy(void* p)
		{
			delete static_cast<T*>(p);
		}

		template <bool Method, class F>
		void native(std::string_view name, F callback) const
		{
			struct box
			{
				const plugin_host* host;
				F callback;
			};

			auto p = std::make_unique<box>(box{ host_, std::move(callback) });
			const auto qualified = prefix_ + std::string(name);
			check(host_->register_native(
				owner_, gsc::string_view(qualified), Method ? 1 : 0,
				[](void* data, uint64_t call, iw8x_entity self) noexcept
				{
					auto& b = *static_cast<box*>(data);
					try
					{
						gsc::context ctx(*b.host, call);
						if constexpr (Method)
							b.callback(ctx, gsc::entity(*b.host, self));
						else
							b.callback(ctx);
					}
					catch (const api_error& e)
					{
						// context::error() has already published the callback's
						// actual message. Do not overwrite it with the SDK's
						// control-flow exception text ("IW8x API status 10").
						if (e.code() != IW8X_CALLBACK_ERROR) b.host->error(call, gsc::string_view(e.what()));
					}
					catch (const std::exception& e)
					{
						b.host->error(call, gsc::string_view(e.what()));
					}
					catch (...)
					{
						b.host->error(call, gsc::string_view("native callback threw"));
					}
				},
				p.get(), &destroy<box>));
			p.release();
		}

		const plugin_host* host_;
		plugin_id owner_;
		std::string prefix_;
	};
}
