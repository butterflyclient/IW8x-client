#pragma once
#include "abi.h"

#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace iw8x::gsc
{
	using vector = iw8x_vector;
	using weapon = iw8x_weapon_snapshot;
	using entity_ref = iw8x_entity;
	enum class value_type : uint32_t
	{
		undefined,
		integer,
		number,
		boolean,
		string,
		vector,
		entity,
		array,
		struct_,
		weapon_ = IW8X_WEAPON_SNAPSHOT
	};

	inline iw8x_string string_view(std::string_view text)
	{
		if (text.size() > UINT32_MAX) throw std::length_error("string too large");
		return { text.data(), static_cast<uint32_t>(text.size()) };
	}

	inline std::string_view text(iw8x_string s)
	{
		if ((!s.data && s.size) || s.size > 1024 * 1024) throw std::invalid_argument("invalid string");
		return s.size ? std::string_view(s.data, s.size) : std::string_view{};
	}

	// Values own their strings/collections in the calling module. Views made by
	// encoded_value borrow from a value, which must remain alive and unchanged.
	class value
	{
	public:
		using array = std::vector<value>;
		using object = std::map<std::string, value, std::less<>>;
		value() = default;

		value(int v) : data_(static_cast<int32_t>(v))
		{
		}

		value(float v) : data_(v)
		{
			if (!std::isfinite(v)) throw std::invalid_argument("nonfinite number");
		}

		value(bool v) : data_(v)
		{
		}

		value(const char* v) : data_(std::string(v ? v : ""))
		{
		}

		value(std::string_view v) : data_(std::string(v))
		{
		}

		value(std::string v) : data_(std::move(v))
		{
		}

		value(vector v) : data_(v)
		{
			if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) throw std::invalid_argument("nonfinite vector");
		}

		value(entity_ref v) : data_(v)
		{
		}

		value(array v) : data_(std::move(v))
		{
		}

		value(object v) : data_(std::move(v))
		{
		}

		value(weapon v) : data_(v)
		{
		}

		value_type type() const noexcept
		{
			return data_.index() == 9 ? value_type::weapon_ : static_cast<value_type>(data_.index());
		}

		const array& as_array() const&
		{
			return get<array>();
		}

		array as_array() &&
		{
			return std::get<array>(std::move(data_));
		}

		const object& as_struct() const&
		{
			return get<object>();
		}

		object as_struct() &&
		{
			return std::get<object>(std::move(data_));
		}

		template <class T>
		const T& get() const
		{
			return std::get<T>(data_);
		}

		template <class T>
		T as() const
		{
			if constexpr (std::is_same_v<T, value>)
				return *this;
			else if constexpr (std::is_same_v<T, std::string_view>)
				return get<std::string>();
			else if constexpr (std::is_same_v<T, bool>)
			{
				// The retail VM encodes GSC booleans as integer 0/1. Other integers
				// are rejected rather than coerced to truthiness.
				if (type() == value_type::integer)
				{
					const auto n = get<int32_t>();
					if (n != 0 && n != 1) throw std::invalid_argument("expected boolean 0 or 1");
					return n != 0;
				}
				return get<bool>();
			}
			else
				return get<T>(); // Exact types only; no int/float/bool reinterpretation.
		}

		static value copy(const iw8x_value& source)
		{
			size_t nodes = 0;
			return copy_impl(source, 0, nodes);
		}

	private:
		static value copy_impl(const iw8x_value& v, unsigned depth, size_t& nodes)
		{
			if (depth > 32 || ++nodes > 65536) throw std::length_error("value nesting/size limit");
			switch (v.type)
			{
			case IW8X_UNDEFINED:
				return {};
			case IW8X_INTEGER:
				return value(v.as.integer);
			case IW8X_NUMBER:
				return value(v.as.number);
			case IW8X_BOOLEAN:
				if (v.as.boolean > 1) throw std::invalid_argument("invalid boolean");
				return value(v.as.boolean != 0);
			case IW8X_STRING:
				return value(text(v.as.string));
			case IW8X_VECTOR:
				return value(v.as.vector);
			case IW8X_ENTITY:
				if (v.as.entity.reserved) throw std::invalid_argument("entity reserved bits");
				return value(v.as.entity);
			case IW8X_ARRAY: {
				if (v.count > 65536 || (v.count && !v.as.elements)) throw std::invalid_argument("invalid array");
				array out;
				out.reserve(v.count);
				for (uint32_t i = 0; i < v.count; ++i)
					out.push_back(copy_impl(v.as.elements[i], depth + 1, nodes));
				return value(std::move(out));
			}
			case IW8X_STRUCT: {
				if (v.count > 65536 || (v.count && !v.as.fields)) throw std::invalid_argument("invalid object");
				object out;
				for (uint32_t i = 0; i < v.count; ++i)
				{
					const auto& f = v.as.fields[i];
					if (!out.emplace(std::string(text(f.name)), copy_impl(f.value, depth + 1, nodes)).second)
						throw std::invalid_argument("duplicate object key");
				}
				return value(std::move(out));
			}
			case IW8X_WEAPON_SNAPSHOT:
				if (!v.as.weapon_snapshot) throw std::invalid_argument("null Weapon snapshot");
				return value(*v.as.weapon_snapshot);
			default:
				throw std::invalid_argument("invalid value type");
			}
		}

		std::variant<std::monostate, int32_t, float, bool, std::string, vector, entity_ref, array, object, weapon> data_;
	};

	class encoded_value
	{
	public:
		explicit encoded_value(const value& v)
		{
			view_.type = static_cast<uint32_t>(v.type());
			switch (v.type())
			{
			case value_type::undefined:
				break;
			case value_type::integer:
				view_.as.integer = v.get<int32_t>();
				break;
			case value_type::number:
				view_.as.number = v.get<float>();
				break;
			case value_type::boolean:
				view_.as.boolean = v.get<bool>() ? 1 : 0;
				break;
			case value_type::string:
				view_.as.string = string_view(v.get<std::string>());
				break;
			case value_type::vector:
				view_.as.vector = v.get<vector>();
				break;
			case value_type::entity:
				view_.as.entity = v.get<entity_ref>();
				break;
			case value_type::array: {
				const auto& a = v.get<value::array>();
				children_.reserve(a.size());
				elements_.reserve(a.size());
				for (const auto& child : a)
					children_.emplace_back(child);
				for (const auto& child : children_)
					elements_.push_back(child.view());
				view_.count = static_cast<uint32_t>(elements_.size());
				view_.as.elements = elements_.data();
				break;
			}
			case value_type::struct_: {
				const auto& o = v.get<value::object>();
				children_.reserve(o.size());
				fields_.reserve(o.size());
				for (const auto& [key, child] : o)
					children_.emplace_back(child);
				size_t i = 0;
				for (const auto& [key, child] : o)
					fields_.push_back({ string_view(key), children_[i++].view() });
				view_.count = static_cast<uint32_t>(fields_.size());
				view_.as.fields = fields_.data();
				break;
			}
			case value_type::weapon_:
				view_.as.weapon_snapshot = &v.get<weapon>();
				break;
			}
		}

		encoded_value(const encoded_value&) = delete;
		encoded_value& operator=(const encoded_value&) = delete;
		encoded_value(encoded_value&&) noexcept = default;

		const iw8x_value& view() const
		{
			return view_;
		}

	private:
		iw8x_value view_{};
		std::vector<encoded_value> children_;
		std::vector<iw8x_value> elements_;
		std::vector<iw8x_field> fields_;
	};
}

namespace iw8x
{
	using value = gsc::value;
	using vec3 = gsc::vector;
	using weapon = gsc::weapon;
	using array = gsc::value::array;
	using struct_value = gsc::value::object;

	// Retail struct fields use canonical IDs, not refcounted string IDs. Keep
	// unknown fields lossless without claiming an unverified name dictionary.
	inline std::string canonical_field(uint32_t id)
	{
		if (!id || id > 0xFFFFFF) throw std::invalid_argument("invalid canonical field ID");
		std::string key = "@000000";
		constexpr char digits[] = "0123456789abcdef";
		for (size_t i = 6; i; --i, id >>= 4)
			key[i] = digits[id & 15];
		return key;
	}

	inline uint32_t canonical_field_id(std::string_view key) noexcept
	{
		if (key.size() != 7 || key[0] != '@') return 0;
		uint32_t id = 0;
		for (size_t i = 1; i != key.size(); ++i)
		{
			const char ch = key[i];
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return 0;
			id = (id << 4) | (ch <= '9' ? ch - '0' : ch - 'a' + 10);
		}
		return id;
	}
}
