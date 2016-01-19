/*
www.github.com/emilk/configuru

Configuru
=========
Configuru, an experimental config library for C++, by Emil Ernerfeldt.

License
-------
This software is in the public domain. Where that dedication is not
recognized, you are granted a perpetual, irrevocable license to copy
and modify this file as you see fit.

That being said, I would appreciate credit!
If you find this library useful, tweet me @ernerfeldt or mail me at emil.ernerfeldt@gmail.com.

Getting started:
----------------
For using:
	`#include <configuru.hpp>`

And in one .cpp file:

	#define CONFIGURU_IMPLEMENTATION 1
	#include <configuru.hpp>

For more info, please se README.md (at www.github.com/emilk/configuru).
*/

//  dP""b8  dP"Yb  88b 88 888888 88  dP""b8 88   88 88""Yb 88   88
// dP   `" dP   Yb 88Yb88 88__   88 dP   `" 88   88 88__dP 88   88
// Yb      Yb   dP 88 Y88 88""   88 Yb  "88 Y8   8P 88"Yb  Y8   8P
//  YboodP  YbodP  88  Y8 88     88  YboodP `YbodP' 88  Yb `YbodP'

#ifndef CONFIGURU_HEADER_HPP
#define CONFIGURU_HEADER_HPP

#include <atomic>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CONFIGURU_ONERROR
	#define CONFIGURU_ONERROR(message_str) \
		throw std::runtime_error(message_str)
#endif // CONFIGURU_ONERROR

#ifndef CONFIGURU_ASSERT
	#include <cassert>
	#define CONFIGURU_ASSERT(test) assert(test)
#endif // CONFIGURU_ASSERT

#ifndef CONFIGURU_ON_DANGLING
	#include <iostream>
	#define CONFIGURU_ON_DANGLING(where, key) \
		std::cerr << where << "Key '" << key << "' never accessed" << std::endl
#endif // CONFIGURU_ON_DANGLING

#define CONFIGURU_NORETURN __attribute__((noreturn))

#ifndef CONFIGURU_IMPLICIT_CONVERSIONS
	#define CONFIGURU_IMPLICIT_CONVERSIONS 0
#endif

#ifndef CONFIGURU_VALUE_SEMANTICS
	// If set, all copies are deep clones.
	#define CONFIGURU_VALUE_SEMANTICS 0
#endif

#define CONFIGURU_NORETURN __attribute__((noreturn))

#undef Bool // Needed on Ubuntu 14.04 with GCC 4.8.5
#undef check // Needed on OSX

namespace configuru
{
	struct DocInfo;
	using DocInfo_SP = std::shared_ptr<DocInfo>;

	struct Include {
		DocInfo_SP doc;
		unsigned line = (unsigned)-1;

		Include() {}
		Include(DocInfo_SP d, unsigned l) : doc(d), line(l) {}
	};

	struct DocInfo {
		std::vector<Include> includers;

		std::string filename;

		DocInfo(const std::string& fn) : filename(fn) { }

		void append_include_info(std::string& ret, const std::string& indent="    ") const;
	};

	struct BadLookupInfo;

	const unsigned BAD_ENTRY = -1;

	template<typename Config_T>
	struct Config_Entry
	{
		Config_T     _value;
		unsigned     _nr       = BAD_ENTRY; // Size of the object prior to adding this entry
		mutable bool _accessed = false;     // Set to true if accessed.
	};

	using Comment = std::string;
	using Comments = std::vector<Comment>;

	struct ConfigComments
	{
		// Comments on preceeding lines.
		// Like this.
		Comments prefix;
		Comments postfix; // After the value, on the same line. Like this.
		Comments pre_end_brace; // Before the closing } or ]

		ConfigComments() {}

		bool empty() const;
		void append(ConfigComments&& other);
	};

	class Config;

	/* Overload this (in cofiguru namespace) for you own types, e.g:

		namespace configuru {
			template<>
			inline Vector2f as(const Config& config)
			{
				auto&& array = config.as_array();
				config.check(array.size() == 2, "Expected Vector2f");
				return {(float)array[0], (float)array[1]};
			}
		}
	*/
	template<typename T>
	inline T as(const configuru::Config& config);

	template<typename ValueType>
	struct KeyValue
	{
		KeyValue(std::string k, ValueType v)
			: key(std::move(k)), value(std::move(v))
		{ }

		std::string key;
		ValueType   value;
	};

	/*
	 A dynamic config variable.
	 Acts like something out of Python or Lua.
	 Uses reference-counting for objects and arrays.
	 This means all copies are shallow copies.
	*/
	class Config
	{
	public:
		enum Type {
			Uninitialized, // Accessing a Config of this type is always an error.
			BadLookupType, // We are the result of a key-lookup in a Object with no hit. We are in effect write-only.
			Null, Bool, Int, Float, String, Array, Object
		};

		using ObjectEntry = Config_Entry<Config>;

		using ConfigArrayImpl = std::vector<Config>;
		using ConfigObjectImpl = std::map<std::string, ObjectEntry>;
		struct ConfigArray
		{
			#if !CONFIGURU_VALUE_SEMANTICS
				std::atomic<unsigned> _ref_count { 1 };
			#endif
			ConfigArrayImpl _impl;
		};
		struct ConfigObject;

		// ----------------------------------------
		// Constructors:

		Config()                     : _type(Uninitialized) { }
		Config(std::nullptr_t)       : _type(Null)    { }
		Config(double f)             : _type(Float)   { _u.f = f; }
		Config(bool b)               : _type(Bool)    { _u.b = b; }
		Config(int i)                : _type(Int)     { _u.i = i; }
		Config(unsigned int i)       : _type(Int)     { _u.i = i; }
		Config(long i)               : _type(Int)     { _u.i = i; }
		Config(unsigned long i)      : _type(Int)     { _u.i = i; }
		Config(long long i)          : _type(Int)     { _u.i = i; }
		Config(unsigned long long i) : _type(Int)
		{
			if ((i & 0x8000000000000000ull) != 0) {
				CONFIGURU_ONERROR("Integer too large to fit into 63 bits");
			}
			_u.i = static_cast<int64_t>(i);
		}
		Config(const char* str);
		Config(std::string str);

		/* Construct an array:
			Config array{1, 2, 3};
		*/
		Config(std::initializer_list<Config> values);

		/* Construct an object:
			Config object{
				KV{"name", "Emil"},
				KV{"age", 30}
			};
		*/
		Config(std::initializer_list<KeyValue<Config>> values);

		template<typename T>
		Config(const std::vector<T>& values) : _type(Uninitialized)
		{
			make_array();
			for (const auto& v : values) {
				push_back(v);
			}
		}

		Config(const std::vector<bool>& values) : _type(Uninitialized)
		{
			make_array();
			for (const auto& v : values) {
				push_back(!!v);
			}
		}

		template<typename T>
		Config(const std::map<std::string, T>& values) : _type(Uninitialized)
		{
			make_object();
			for (const auto& p : values) {
				(*this)[p.first] = p.second;
			}
		}

		// Used by the parser - no need to use directly.
		void make_object();
		void make_array();
		void tag(const DocInfo_SP& doc, unsigned line, unsigned column);

		// Explicitly create an object:
		static Config object();
		static Config object(std::initializer_list<KeyValue<Config>> values);

		// Explicitly create an array:
		static Config array();
		static Config array(std::initializer_list<Config> values);

		// ----------------------------------------

		~Config() { free(); }

		Config(const Config& o);

		Config(Config&& o) noexcept { this->swap(o); }

		Config& operator=(const Config& o);

		// Will still remmeber file/line when assigned an object which has no file/line
		Config& operator=(Config&& o) noexcept;

		// Swaps file/line too.
		void swap(Config& o) noexcept;

		#ifdef CONFIG_EXTENSION
			CONFIG_EXTENSION
		#endif

		// ----------------------------------------
		// Inspectors:

		Type type() const { return _type; }

		bool is_uninitialized() const { return _type == Uninitialized;    }
		bool is_null()          const { return _type == Null;             }
		bool is_bool()          const { return _type == Bool;             }
		bool is_int()           const { return _type == Int;              }
		bool is_float()         const { return _type == Float;            }
		bool is_string()        const { return _type == String;           }
		bool is_object()        const { return _type == Object;           }
		bool is_array()         const { return _type == Array;            }
		bool is_number()        const { return is_int() || is_float();    }

		// Returns file:line iff available.
		std::string where() const;

		// -1 if not set.
		unsigned line() const { return _line; }

		// Handle to document.
		const DocInfo_SP& doc() const { return _doc; }
		void set_doc(const DocInfo_SP& doc) { _doc = doc; }

		// ----------------------------------------
		// Convertors:

#if CONFIGURU_IMPLICIT_CONVERSIONS
		// Explicit casting, for overloads of as<T>
		template<typename T>
		explicit operator T() const { return as<T>(*this); }

		inline operator bool()                    const { return as_bool();   }
		inline operator signed char()             const { return as_integer<signed char>();         }
		inline operator unsigned char()           const { return as_integer<unsigned char>();       }
		inline operator signed short()            const { return as_integer<signed short>();         }
		inline operator unsigned short()          const { return as_integer<unsigned short>();       }
		inline operator signed int()              const { return as_integer<signed int>();         }
		inline operator unsigned int()            const { return as_integer<unsigned int>();       }
		inline operator signed long()             const { return as_integer<signed long>();        }
		inline operator unsigned long()           const { return as_integer<unsigned long>();      }
		inline operator signed long long()        const { return as_integer<signed long long>();   }
		inline operator unsigned long long()      const { return as_integer<unsigned long long>(); }
		inline operator float()                   const { return as_float();  }
		inline operator double()                  const { return as_double(); }
		inline operator std::string()             const { return as_string(); }
		inline operator Config::ConfigArrayImpl() const { return as_array();  }

		// Convenience:
		template<typename T>
		operator std::vector<T>() const
		{
			const auto& array = as_array();
			std::vector<T> ret;
			ret.reserve(array.size());
			for (auto&& config : array) {
				ret.push_back((T)config);
			}
			return ret;
		}

		// Convenience: TODO: generalize for tuples.
		template<typename Left, typename Right>
		operator std::pair<Left, Right>() const
		{
			const auto& array = as_array();
			check(array.size() == 2u, "Mismatched array length.");
			return {(Left)array[0], (Right)array[1]};
		}
#else
		// Explicit casting, since C++ handles implicit casts real badly.
		template<typename T>
		explicit operator T() const { return as<T>(*this); }

		// Convenience:
		template<typename T>
		explicit operator std::vector<T>() const
		{
			const auto& array = as_array();
			std::vector<T> ret;
			ret.reserve(array.size());
			for (auto&& config : array) {
				ret.push_back((T)config);
			}
			return ret;
		}

		// Convenience: TODO: generalize for tuples.
		template<typename Left, typename Right>
		explicit operator std::pair<Left, Right>() const
		{
			const auto& array = as_array();
			check(array.size() == 2u, "Mismatched array length.");
			return {(Left)array[0], (Right)array[1]};
		}
#endif

		const std::string& as_string() const { assert_type(String); return *_u.str; }
		const char* c_str() const { assert_type(String); return _u.str->c_str(); }

		bool as_bool() const
		{
			assert_type(Bool);
			return _u.b;
		}

		template<typename IntT>
		IntT as_integer() const
		{
			static_assert(std::is_integral<IntT>::value, "Not an integer.");
			assert_type(Int);
			check((int64_t)(IntT)_u.i == _u.i, "Integer out of range");
			return static_cast<IntT>(_u.i);
		}

		float as_float() const
		{
			if (_type == Int) {
				return _u.i;
			} else {
				assert_type(Float);
				return (float)_u.f;
			}
		}

		double as_double() const
		{
			if (_type == Int) {
				return _u.i;
			} else {
				assert_type(Float);
				return _u.f;
			}
		}

		template<typename T>
		T get() const;

		// Returns the value or `default_value` if this is the result of a bad lookup.
		template<typename T>
		T get_or(const T& default_value) const
		{
			if (_type == BadLookupType) {
				return default_value;
			} else {
				return (T)*this;
			}
		}

		// ----------------------------------------
		// Array:

		size_t array_size() const
		{
			return as_array().size();
		}

		// for (Config& e : cfg.as_array()) { ... }
		ConfigArrayImpl& as_array()
		{
			assert_type(Array);
			return _u.array->_impl;
		}

		// for (const Config& e : cfg.as_array()) { ... }
		const ConfigArrayImpl& as_array() const
		{
			assert_type(Array);
			return _u.array->_impl;
		}

		Config& operator[](size_t ix)
		{
			auto&& array = as_array();
			check(ix < array.size(), "Array index out of range");
			return array[ix];
		}

		const Config& operator[](size_t ix) const
		{
			auto&& array = as_array();
			check(ix < array.size(), "Array index out of range");
			return array[ix];
		}

		void push_back(Config value)
		{
			as_array().push_back(std::move(value));
		}

		// ----------------------------------------
		// Object:

		size_t object_size() const;

		// for (auto& p : cfg.as_object()) { p.value() = p.key(); }
		ConfigObject& as_object()
		{
			assert_type(Object);
			return *_u.object;
		}

		// for (const auto& p : cfg.as_object()) { cout << p.key() << ": " << p.value(); }
		const ConfigObject& as_object() const
		{
			assert_type(Object);
			return *_u.object;
		}

		const Config& operator[](const std::string& key) const;

		// Prefer `obj.insert_or_assign(key, value);` to `obj[key] = value;` when inserting and performance is important!
		Config& operator[](const std::string& key);

		// For indexing with string literals:
		template<std::size_t N>
		Config& operator[](const char (&key)[N]) { return operator[](std::string(key)); }
		template<std::size_t N>
		const Config& operator[](const char (&key)[N]) const { return operator[](std::string(key)); }

		bool has_key(const std::string& key) const;
		size_t count(const std::string& key) const { return has_key(key) ? 1 : 0; }

		void insert_or_assign(const std::string& key, Config&& config);

		bool erase(const std::string& key);

		template<typename T>
		T get(const std::string& key) const
		{
			return as<T>((*this)[key]);
		}

		template<typename T>
		T get_or(const std::string& key, const T& default_value) const;

		std::string get_or(const std::string& key, const char* default_value) const
		{
			return get_or<std::string>(key, default_value);
		}

		// --------------------------------------------------------------------------------

		// Compare Config values recursively.
		static bool deep_eq(const Config& a, const Config& b);

#if !CONFIGURU_VALUE_SEMANTICS // No need for a deep_clone method when all copies are deep clones.
		// Copy this Config value recursively.
		Config deep_clone() const;
#endif

		// ----------------------------------------

		// Will check for dangling (unaccessed) object keys recursively,
		void check_dangling() const;

		// Set the 'access' flag recursively,
		void mark_accessed(bool v) const;

		// ----------------------------------------

		bool has_comments() const
		{
			return _comments && !_comments->empty();
		}

		// Read/write of comments.
		ConfigComments& comments()
		{
			if (!_comments) {
				_comments.reset(new ConfigComments());
			}
			return *_comments;
		}

		// Read comments.
		const ConfigComments& comments() const
		{
			static const ConfigComments s_empty {};
			if (_comments) {
				return *_comments;
			} else {
				return s_empty;
			}
		}

		// Returns either "true", "false", the constained string, or the type name.
		const char* debug_descr() const;

		// Human-readable version of the type ("integer", "bool", etc).
		static const char* type_str(Type t);

		// ----------------------------------------
		// Helper functions for checking the type is what we expect:

		inline void check(bool b, const char* msg) const
		{
			if (!b) {
				on_error(msg);
			}
		}

		void assert_type(Type t) const;

		void on_error(const std::string& msg) const CONFIGURU_NORETURN;

	private:
		void free();

		using ConfigComments_UP = std::unique_ptr<ConfigComments>;

		Type  _type = Uninitialized;

		union {
			bool           b;
			int64_t        i;
			double         f;
			std::string*   str;
			ConfigObject*  object;
			ConfigArray*   array;
			BadLookupInfo* bad_lookup;
		} _u;

		DocInfo_SP        _doc; // So we can name the file
		unsigned          _line = (unsigned)-1; // Where in the source, or -1. Lines are 1-indexed.
		ConfigComments_UP _comments;
	};

	// ------------------------------------------------------------------------

	struct Config::ConfigObject
	{
		#if !CONFIGURU_VALUE_SEMANTICS
			std::atomic<unsigned> _ref_count { 1 };
		#endif
		ConfigObjectImpl      _impl;

		class iterator
		{
		public:
			explicit iterator(ConfigObjectImpl::iterator it) : _it(std::move(it)) {}

			const iterator& operator*() const {
				_it->second._accessed = true;
				return *this;
			}

			iterator& operator++() {
				++_it;
				return *this;
			}

			friend bool operator!=(const iterator& a, const iterator& b) {
				return a._it != b._it;
			}

			const std::string& key()   const { return _it->first;         }
			Config&            value() const { return _it->second._value; }

		private:
			ConfigObjectImpl::iterator _it;
		};

		class const_iterator
		{
		public:
			explicit const_iterator(ConfigObjectImpl::const_iterator it) : _it(std::move(it)) {}

			const const_iterator& operator*() const {
				_it->second._accessed = true;
				return *this;
			}

			const_iterator& operator++() {
				++_it;
				return *this;
			}

			friend bool operator!=(const const_iterator& a, const const_iterator& b) {
				return a._it != b._it;
			}

			const std::string& key()   const { return _it->first;         }
			const Config&      value() const { return _it->second._value; }

		private:
			ConfigObjectImpl::const_iterator _it;
		};

		iterator      begin()         { return iterator{_impl.begin()};        }
		iterator      end()           { return iterator{_impl.end()};          }
		const_iterator begin()  const { return const_iterator{_impl.cbegin()}; }
		const_iterator end()    const { return const_iterator{_impl.cend()};   }
		const_iterator cbegin() const { return const_iterator{_impl.cbegin()}; }
		const_iterator cend()   const { return const_iterator{_impl.cend()};   }
	};

	using KV = KeyValue<Config>;

	// ------------------------------------------------------------------------

	inline bool operator==(const Config& a, const Config& b)
	{
		return Config::deep_eq(a, b);
	}
	inline bool operator!=(const Config& a, const Config& b)
	{
		return !Config::deep_eq(a, b);
	}

	// ------------------------------------------------------------------------

	template<> inline bool                           Config::get() const { return as_bool();   }
	template<> inline signed char                    Config::get() const { return as_integer<signed char>();         }
	template<> inline unsigned char                  Config::get() const { return as_integer<unsigned char>();       }
	template<> inline signed short                   Config::get() const { return as_integer<signed short>();         }
	template<> inline unsigned short                 Config::get() const { return as_integer<unsigned short>();       }
	template<> inline signed int                     Config::get() const { return as_integer<signed int>();         }
	template<> inline unsigned int                   Config::get() const { return as_integer<unsigned int>();       }
	template<> inline signed long                    Config::get() const { return as_integer<signed long>();        }
	template<> inline unsigned long                  Config::get() const { return as_integer<unsigned long>();      }
	template<> inline signed long long               Config::get() const { return as_integer<signed long long>();   }
	template<> inline unsigned long long             Config::get() const { return as_integer<unsigned long long>(); }
	template<> inline float                          Config::get() const { return as_float();  }
	template<> inline double                         Config::get() const { return as_double(); }
	template<> inline const std::string&             Config::get() const { return as_string(); }
	template<> inline std::string                    Config::get() const { return as_string(); }
	template<> inline const Config::ConfigArrayImpl& Config::get() const { return as_array();  }
	// template<> inline std::vector<std::string>     Config::get() const { return as_vector<T>();   }

	// ------------------------------------------------------------------------

	template<typename T>
	inline T as(const configuru::Config& config)
	{
		return config.get<T>();
	}

	template<typename T>
	T Config::get_or(const std::string& key, const T& default_value) const
	{
		auto&& object = as_object()._impl;
		auto it = object.find(key);
		if (it == object.end()) {
			return default_value;
		} else {
			const auto& entry = it->second;
			entry._accessed = true;
			return as<T>(entry._value);
		}
	}

	// ------------------------------------------------------------------------

	// Prints in JSON but in a fail-safe mannor, allowing uninitalized keys and inf/nan.
	std::ostream& operator<<(std::ostream& os, const Config& cfg);

	// ------------------------------------------------------------------------

	template<class Config, class Visitor>
	void visit_configs(Config&& config, Visitor&& visitor)
	{
		visitor(config);
		if (config.is_object()) {
			for (auto&& p : config.as_object()) {
				visit_configs(p.value(), visitor);
			}
		} else if (config.is_array()) {
			for (auto&& e : config.as_array()) {
				visit_configs(e, visitor);
			}
		}
	}

	inline void clear_doc(Config& root) // TODO: shouldn't be needed. Replace with some info of wether a Config is the root of the document it is in.
	{
		visit_configs(root, [&](Config& cfg){ cfg.set_doc(nullptr); });
	}

	/*
	inline void replace_doc(Config& root, DocInfo_SP find, DocInfo_SP replacement)
	{
		visit_configs(root, [&](Config& config){
			if (config.doc() == find) {
				config.set_doc(replacement);
			}
		});
	}

	// Will try to merge from 'src' do 'dst', replacing with 'src' on any conflict.
	inline void merge_replace(Config& dst, const Config& src)
	{
		if (dst.is_object() && src.is_object()) {
			for (auto&& p : src.as_object()) {
				merge_replace(dst[p.key()], p.value());
			}
		} else {
			dst = src;
		}
	}
	 */

	// ----------------------------------------------------------

	class ParseError : public std::exception
	{
	public:
		ParseError(const DocInfo_SP& doc, unsigned line, unsigned column, const std::string& msg)
			: _line(line), _column(column)
		{
			_what = doc->filename + ":" + std::to_string(line) + ":" + std::to_string(column);
			doc->append_include_info(_what);
			_what += ": " + msg;
		}

		const char* what() const noexcept override
		{
			return _what.c_str();
		}

		unsigned line()   const noexcept { return _line; }
		unsigned column() const noexcept { return _column; }

	private:
		unsigned _line, _column;
		std::string _what;
	};

	// ----------------------------------------------------------

	struct FormatOptions
	{
		// This struct basically contain all differences with JSON.

		/* Indentation should be a single tab,
		   multiple spaces or an empty string.
		   An empty string means the output will be compact. */
		std::string indentation              = "\t";
		bool        enforce_indentation      = true;  // Must have correct indentation?

		// Top file:
		bool        empty_file               = false; // If true, an empty file is an empty object.
		bool        implicit_top_object      = true;  // Ok with key-value pairs top-level?
		bool        implicit_top_array       = true;  // Ok with several values top-level?

		// Comments:
		bool        single_line_comments     = true;  // Allow this?
		bool        block_comments           = true;  /* Allow this? */
		bool        nesting_block_comments   = true;  // /* Allow /*    this? */ */

		// Numbers:
		bool        inf                      = true;  // Allow +inf, -inf
		bool        nan                      = true;  // Allow +NaN
		bool        hexadecimal_integers     = true;  // Allow 0xff
		bool        binary_integers          = true;  // Allow 0b1010
		bool        unary_plus               = true;  // Allow +42
		bool        distinct_floats          = true;  // Print 9.0 as "9.0", not just "9". A must for round-tripping.

		// Arrays
		bool        array_omit_comma         = true;  // Allow [1 2 3]
		bool        array_trailing_comma     = true;  // Allow [1, 2, 3,]

		// Objects:
		bool        identifiers_keys         = true;  // { is_this_ok: true }
		bool        object_separator_equal   = false; // { "is_this_ok" = true }
		bool        allow_space_before_colon = false; // { "is_this_ok" : true }
		bool        omit_colon_before_object = false; // { "nested_object" { } }
		bool        object_omit_comma        = true;  // Allow {a:1 b:2}
		bool        object_trailing_comma    = true;  // Allow {a:1, b:2,}
		bool        object_duplicate_keys    = false; // Allow {"a":1, "a":2}

		// Strings
		bool        str_csharp_verbatim      = true;  // Allow @"Verbatim\strings"
		bool        str_python_multiline     = true;  // Allow """ Python\nverbatim strings """
		bool        str_32bit_unicode        = true;  // Allow "\U0030dbfd"
		bool        str_allow_tab            = true;  // Allow unescaped tab in string.

		// Special
		bool        allow_macro              = true;  // Allow #include "some_other_file.cfg"

		// When writing:
		bool        write_comments           = true;

		// Sort keys lexicographically. If false, sort by order they where added.
		bool        sort_keys                = false;

		// When printing, write uninitialized values as UNINITIALIZED. Useful for debugging.
		bool        write_uninitialized      = false;

		bool compact() const { return indentation.empty(); }
	};

	inline FormatOptions make_json_options()
	{
		FormatOptions options;

		options.indentation              = "\t";
		options.enforce_indentation      = false;

		// Top file:
		options.empty_file               = false;
		options.implicit_top_object      = false;
		options.implicit_top_array       = false;

		// Comments:
		options.single_line_comments     = false;
		options.block_comments           = false;
		options.nesting_block_comments   = false;

		// Numbers:
		options.inf                      = false;
		options.nan                      = false;
		options.hexadecimal_integers     = false;
		options.binary_integers          = false;
		options.unary_plus               = false;
		options.distinct_floats          = true;

		// Arrays
		options.array_omit_comma         = false;
		options.array_trailing_comma     = false;

		// Objects:
		options.identifiers_keys         = false;
		options.object_separator_equal   = false;
		options.allow_space_before_colon = true;
		options.omit_colon_before_object = false;
		options.object_omit_comma        = false;
		options.object_trailing_comma    = false;
		options.object_duplicate_keys    = false; // To be 100% JSON compatile, this should be true, but it is error prone.

		// Strings
		options.str_csharp_verbatim      = false;
		options.str_python_multiline     = false;
		options.str_32bit_unicode        = false;
		options.str_allow_tab            = false;

		// Special
		options.allow_macro              = false;

		// When writing:
		options.write_comments           = false;
		options.sort_keys                = false;

		return options;
	}

	inline FormatOptions make_forgiving_options()
	{
		FormatOptions options;

		options.indentation              = "\t";
		options.enforce_indentation      = false;

		// Top file:
		options.empty_file               = true;
		options.implicit_top_object      = true;
		options.implicit_top_array       = true;

		// Comments:
		options.single_line_comments     = true;
		options.block_comments           = true;
		options.nesting_block_comments   = true;

		// Numbers:
		options.inf                      = true;
		options.nan                      = true;
		options.hexadecimal_integers     = true;
		options.binary_integers          = true;
		options.unary_plus               = true;
		options.distinct_floats          = true;

		// Arrays
		options.array_omit_comma         = true;
		options.array_trailing_comma     = true;

		// Objects:
		options.identifiers_keys         = true;
		options.object_separator_equal   = true;
		options.allow_space_before_colon = true;
		options.omit_colon_before_object = true;
		options.object_omit_comma        = true;
		options.object_trailing_comma    = true;
		options.object_duplicate_keys    = true;

		// Strings
		options.str_csharp_verbatim      = true;
		options.str_python_multiline     = true;
		options.str_32bit_unicode        = true;
		options.str_allow_tab            = true;

		// Special
		options.allow_macro              = true;

		// When writing:
		options.write_comments           = false;
		options.sort_keys                = false;

		return options;
	}

	static const FormatOptions CFG       = FormatOptions();
	static const FormatOptions JSON      = make_json_options();
	static const FormatOptions FORGIVING = make_forgiving_options();

	struct ParseInfo {
		std::map<std::string, Config> parsed_files; // Two #include gives same Config tree.
	};

	// The parser may throw ParseError.
	// `str` should be a zero-ended Utf-8 encoded string of characters.
	// The `name` should be something akin to a filename. It is only for error reporting.
	Config parse_string(const char* str, const FormatOptions& options, const char* name);
	Config parse_file(const std::string& path, const FormatOptions& options);

	// Advanced usage:
	Config parse_string(const char* str, const FormatOptions& options, DocInfo _doc, ParseInfo& info);
	Config parse_file(const std::string& path, const FormatOptions& options, DocInfo_SP doc, ParseInfo& info);

	// ----------------------------------------------------------
	// Writes the config as a string in the given format.
	// May call CONFIGURU_ONERROR if the given config is invalid. This can happen if
	// a Config is unitialized (and options write_uninitialized is not set) or
	// a Config contains inf/nan (and options.inf/options.nan aren't set).
	std::string dump_string(const Config& config, const FormatOptions& options);

	// Writes the config to a file. Like dump_string, but can may also call CONFIGURU_ONERROR
	// if it fails to write to the given path.
	void dump_file(const std::string& path, const Config& config, const FormatOptions& options);
} // namespace configuru

#endif // CONFIGURU_HEADER_HPP

// ----------------------------------------------------------------------------
// 88 8b    d8 88""Yb 88     888888 8b    d8 888888 88b 88 888888    db    888888 88  dP"Yb  88b 88
// 88 88b  d88 88__dP 88     88__   88b  d88 88__   88Yb88   88     dPYb     88   88 dP   Yb 88Yb88
// 88 88YbdP88 88"""  88  .o 88""   88YbdP88 88""   88 Y88   88    dP__Yb    88   88 Yb   dP 88 Y88
// 88 88 YY 88 88     88ood8 888888 88 YY 88 888888 88  Y8   88   dP""""Yb   88   88  YbodP  88  Y8

/* In one of your .cpp files you need to do the following:
#define CONFIGURU_IMPLEMENTATION
#include <configuru.hpp>

This will define all the Configuru functions so that the linker may find them.
*/

#if defined(CONFIGURU_IMPLEMENTATION) && !defined(CONFIGURU_HAS_BEEN_IMPLEMENTED)
#define CONFIGURU_HAS_BEEN_IMPLEMENTED

// ----------------------------------------------------------------------------
namespace configuru
{
	void DocInfo::append_include_info(std::string& ret, const std::string& indent) const
	{
		if (!includers.empty()) {
			ret += ", included at:\n";
			for (auto&& includer : includers) {
				ret += indent + includer.doc->filename + ":" + std::to_string(includer.line);
				includer.doc->append_include_info(ret, indent + "    ");
				ret += "\n";
			}
			ret.pop_back();
		}
	}

	struct BadLookupInfo
	{
		const DocInfo_SP      doc;      // Of parent object
		const unsigned        line;     // Of parent object
		const std::string     key;

		#if !CONFIGURU_VALUE_SEMANTICS
			std::atomic<unsigned> _ref_count { 1 };
		#endif

		BadLookupInfo(DocInfo_SP doc_, unsigned line_, std::string key_)
			: doc(std::move(doc_)), line(line_), key(std::move(key_)) {}
	};

	Config::Config(const char* str) : _type(String)
	{
		CONFIGURU_ASSERT(str != nullptr);
		_u.str = new std::string(str);
	}

	Config::Config(std::string str) : _type(String)
	{
		_u.str = new std::string(move(str));
	}

	Config::Config(std::initializer_list<Config> values) : _type(Uninitialized)
	{
		CONFIGURU_ASSERT(values.size() != 0);
		make_array();
		for (auto&& v : values) {
			push_back(std::move(v));
		}
	}

	Config::Config(std::initializer_list<KeyValue<Config>> values) : _type(Uninitialized)
	{
		CONFIGURU_ASSERT(values.size() != 0);
		make_object();
		for (auto&& p : values) {
			(*this)[p.key] = std::move(p.value);
		}
	}

	void Config::make_object()
	{
		assert_type(Uninitialized);
		_type = Object;
		_u.object = new ConfigObject();
	}

	void Config::make_array()
	{
		assert_type(Uninitialized);
		_type = Array;
		_u.array = new ConfigArray();
	}

	Config Config::object()
	{
		Config ret;
		ret.make_object();
		return ret;
	}

	Config Config::object(std::initializer_list<KeyValue<Config>> values)
	{
		Config ret;
		ret.make_object();
		for (auto&& p : values) {
			ret[p.key] = std::move(p.value);
		}
		return ret;
	}

	Config Config::array()
	{
		Config ret;
		ret.make_array();
		return ret;
	}

	Config Config::array(std::initializer_list<Config> values)
	{
		Config ret;
		ret.make_array();
		for (auto&& v : values) {
			ret.push_back(std::move(v));
		}
		return ret;
	}

	void Config::tag(const DocInfo_SP& doc, unsigned line, unsigned column)
	{
		_doc = doc;
		_line = line;
		(void)column; // TODO: include this info too.
	}

	// ----------------------------------------

	Config::Config(const Config& o) : _type(Uninitialized)
	{
		*this = o;
	}

	void Config::swap(Config& o) noexcept
	{
		std::swap(_type,     o._type);
		std::swap(_u,        o._u);
		std::swap(_doc,      o._doc);
		std::swap(_line,     o._line);
		std::swap(_comments, o._comments);
	}

	Config& Config::operator=(const Config& o)
	{
		// Remember to remember where we come from even when assigned a new value.
		if (&o == this) { return *this; }
		free();
		_type = o._type;
		if (_type == String) {
			_u.str = new std::string(*o._u.str);
#if CONFIGURU_VALUE_SEMANTICS
		} else if (_type == BadLookupType) {
			_u.bad_lookup = new BadLookupInfo(*o._u.bad_lookup);
		} else if (_type == Object) {
			_u.object = new ConfigObject(*o._u.object);
		} else if (_type == Array) {
			_u.array = new ConfigArray(*o._u.array);
#endif
		} else {
			memcpy(&_u, &o._u, sizeof(_u));
			#if !CONFIGURU_VALUE_SEMANTICS
				if (_type == BadLookupType) { _u.bad_lookup->_ref_count += 1; }
				if (_type == Array)         { _u.array->_ref_count      += 1; }
				if (_type == Object)        { _u.object->_ref_count     += 1; }
			#endif
		}

		if (o._doc) {
			_doc      = o._doc;
			_line     = o._line;
		} else if (o._line != (unsigned)-1) {
			_doc  = nullptr;
			_line = o._line;
		} else {
			// Keep our _doc/_line
		}

		if (o._comments) {
			_comments.reset(new ConfigComments(*o._comments));
		} else {
			// Keep our _comments
		}
		return *this;
	}

	Config& Config::operator=(Config&& o) noexcept
	{
		// Remember to remember where we come from even when assigned a new value.
		std::swap(_type,     o._type);
		std::swap(_u,        o._u);
		if (o._doc || o._line != (unsigned)-1) {
			std::swap(_doc,      o._doc);
			std::swap(_line,     o._line);
		}
		if (o._comments) {
			std::swap(_comments, o._comments);
		}

		return *this;
	}

	size_t Config::object_size() const
	{
		return as_object()._impl.size();
	}

	const Config& Config::operator[](const std::string& key) const
	{
		auto&& object = as_object()._impl;
		auto it = object.find(key);
		if (it == object.end()) {
			on_error("Key '" + key + "' not in object");
		} else {
			const auto& entry = it->second;
			entry._accessed = true;
			return entry._value;
		}
	}

	Config& Config::operator[](const std::string& key)
	{
		auto&& object = as_object()._impl;
		auto&& entry = object[key];
		if (entry._nr == BAD_ENTRY) {
			// New entry
			entry._nr = (unsigned)object.size() - 1;
			entry._value._type = BadLookupType;
			entry._value._u.bad_lookup = new BadLookupInfo{_doc, _line, key};
		} else {
			entry._accessed = true;
		}
		return entry._value;
	}

	bool Config::has_key(const std::string& key) const
	{
		return as_object()._impl.count(key) != 0;
	}

	void Config::insert_or_assign(const std::string& key, Config&& config)
	{
		auto&& object = as_object()._impl;
		auto&& entry = object[key];
		if (entry._nr == BAD_ENTRY) {
			// New entry
			entry._nr = (unsigned)object.size() - 1;
		} else {
			entry._accessed = true;
		}
		entry._value = std::move(config);
	}

	bool Config::erase(const std::string& key)
	{
		auto& object = as_object()._impl;
		auto it = object.find(key);
		if (it == object.end()) {
			return false;
		} else {
			object.erase(it);
			return true;
		}
	}

	bool Config::deep_eq(const Config& a, const Config& b)
	{
		if (a._type != b._type) { return false; }
		if (a._type == Null)   { return true; }
		if (a._type == Bool)   { return a._u.b    == b._u.b;    }
		if (a._type == Int)    { return a._u.i    == b._u.i;    }
		if (a._type == Float)  { return a._u.f    == b._u.f;    }
		if (a._type == String) { return *a._u.str == *b._u.str; }
		if (a._type == Object)    {
			if (a._u.object == b._u.object) { return true; }
			auto&& a_object = a.as_object()._impl;
			auto&& b_object = b.as_object()._impl;
			if (a_object.size() != b_object.size()) { return false; }
			for (auto&& p: a_object) {
				auto it = b_object.find(p.first);
				if (it == b_object.end()) { return false; }
				if (!deep_eq(p.second._value, it->second._value)) { return false; }
			}
			return true;
		}
		if (a._type == Array)    {
			if (a._u.array == b._u.array) { return true; }
			auto&& a_array = a.as_array();
			auto&& b_array = b.as_array();
			if (a_array.size() != b_array.size()) { return false; }
			for (size_t i=0; i<a_array.size(); ++i) {
				if (!deep_eq(a_array[i], a_array[i])) {
					return false;
				}
			}
			return true;
		}

		return false;
	}

#if !CONFIGURU_VALUE_SEMANTICS
	Config Config::deep_clone() const
	{
		Config ret = *this;
		if (ret._type == Object) {
			ret = Config::object();
			for (auto&& p : this->as_object()._impl) {
				auto& dst = ret._u.object->_impl[p.first];
				dst._nr    = p.second._nr;
				dst._value = p.second._value.deep_clone();
			}
		}
		if (ret._type == Array) {
			ret = Config::array();
			for (auto&& value : this->as_array()) {
				ret.push_back( value.deep_clone() );
			}
		}
		return ret;
	}
#endif

	void Config::check_dangling() const
	{
		// TODO: iterating over an object should count as accessing the values
		if (is_object()) {
			for (auto&& p : as_object()._impl) {
				auto&& entry = p.second;
				auto&& value = entry._value;
				if (entry._accessed) {
					value.check_dangling();
				} else {
					auto where_str = value.where();
					CONFIGURU_ON_DANGLING(where_str.c_str(), p.first.c_str());
				}
			}
		} else if (is_array()) {
			for (auto&& e : as_array()) {
				e.check_dangling();
			}
		}
	}

	void Config::mark_accessed(bool v) const
	{
		if (is_object()) {
			for (auto&& p : as_object()._impl) {
				auto&& entry = p.second;
				entry._accessed = v;
				entry._value.mark_accessed(v);
			}
		} else if (is_array()) {
			for (auto&& e : as_array()) {
				e.mark_accessed(v);
			}
		}
	}

	const char* Config::debug_descr() const {
		switch (_type) {
			case Bool:   return _u.b ? "true" : "false";
			case String: return _u.str->c_str();
			default:     return type_str(_type);
		}
	}

	const char* Config::type_str(Type t)
	{
		switch (t) {
			case Uninitialized: return "uninitialized";
			case BadLookupType: return "undefined";
			case Null:          return "null";
			case Bool:          return "bool";
			case Int:           return "integer";
			case Float:         return "float";
			case String:        return "string";
			case Array:         return "array";
			case Object:        return "object";
			default:            return "BROKEN Config";
		}
	}

	void Config::free()
	{
		#if CONFIGURU_VALUE_SEMANTICS
			if (_type == BadLookupType) {
				delete _u.bad_lookup;
			} else if (_type == Object) {
				delete _u.object;
			} else if (_type == Array) {
				delete _u.array;
		#else
			if (_type == BadLookupType) {
				if (--_u.bad_lookup->_ref_count == 0) {
					delete _u.bad_lookup;
				}
			} else if (_type == Object) {
				if (--_u.object->_ref_count == 0) {
					delete _u.object;
				}
			} else if (_type == Array) {
				if (--_u.array->_ref_count == 0) {
					delete _u.array;
				}
		#endif
		} else if (_type == String) {
			delete _u.str;
		}
		_type = Uninitialized;
	}

	std::string where_is(const DocInfo_SP& doc, unsigned line)
	{
		if (doc) {
			std::string ret = doc->filename;
			if (line != BAD_ENTRY) {
				ret += ":" + std::to_string(line);
			}
			doc->append_include_info(ret);
			ret += ": ";
			return ret;
		} else if (line != BAD_ENTRY) {
			return "line " + std::to_string(line) + ": ";
		} else {
			return "";
		}
	}

	std::string Config::where() const
	{
		return where_is(_doc, _line);
	}

	void Config::on_error(const std::string& msg) const
	{
		CONFIGURU_ONERROR(where() + msg);
		abort(); // We shouldn't get here.
	}

	void Config::assert_type(Type exepected) const
	{
		if (_type == BadLookupType) {
			auto where = where_is(_u.bad_lookup->doc, _u.bad_lookup->line);
			CONFIGURU_ONERROR(where + "Failed to find key '" + _u.bad_lookup->key + "'");
		} else if (_type != exepected) {
			const auto message = where() + "Expected " + type_str(exepected) + ", got " + type_str(_type);
			if (_type == Uninitialized && exepected == Object) {
				CONFIGURU_ONERROR(message + ". Did you forget to call Config::object()?");
			} else if (_type == Uninitialized && exepected == Array) {
				CONFIGURU_ONERROR(message + ". Did you forget to call Config::array()?");
			} else {
				CONFIGURU_ONERROR(message);
			}
		}
	}

	std::ostream& operator<<(std::ostream& os, const Config& cfg)
	{
		auto format = JSON;
		// Make sure that all config types are serializable:
		format.inf                 = true;
		format.nan                 = true;
		format.write_uninitialized = true;
		return os << dump_string(cfg, format);
	}
}

// ----------------------------------------------------------------------------
// 88""Yb    db    88""Yb .dP"Y8 888888 88""Yb
// 88__dP   dPYb   88__dP `Ybo." 88__   88__dP
// 88"""   dP__Yb  88"Yb  o.`Y8b 88""   88"Yb
// 88     dP""""Yb 88  Yb 8bodP' 888888 88  Yb

#include <cerrno>
#include <cstdlib>

namespace configuru
{
	using namespace configuru;

	void append(Comments& a, Comments&& b)
	{
		for (auto&& entry : b) {
			a.emplace_back(std::move(entry));
		}
	}

	bool ConfigComments::empty() const
	{
		return prefix.empty()
			&& postfix.empty()
			&& pre_end_brace.empty();
	}

	void ConfigComments::append(ConfigComments&& other)
	{
		configuru::append(this->prefix,        std::move(other.prefix));
		configuru::append(this->postfix,       std::move(other.postfix));
		configuru::append(this->pre_end_brace, std::move(other.pre_end_brace));
	}

	// Returns the number of bytes written, or 0 on error
	size_t encode_utf8(std::string& dst, uint64_t c)
	{
		if (c <= 0x7F)  // 0XXX XXXX - one byte
		{
			dst += (char) c;
			return 1;
		}
		else if (c <= 0x7FF)  // 110X XXXX - two bytes
		{
			dst += (char) ( 0xC0 | (c >> 6) );
			dst += (char) ( 0x80 | (c & 0x3F) );
			return 2;
		}
		else if (c <= 0xFFFF)  // 1110 XXXX - three bytes
		{
			dst += (char) (0xE0 | (c >> 12));
			dst += (char) (0x80 | ((c >> 6) & 0x3F));
			dst += (char) (0x80 | (c & 0x3F));
			return 3;
		}
		else if (c <= 0x1FFFFF)  // 1111 0XXX - four bytes
		{
			dst += (char) (0xF0 | (c >> 18));
			dst += (char) (0x80 | ((c >> 12) & 0x3F));
			dst += (char) (0x80 | ((c >> 6) & 0x3F));
			dst += (char) (0x80 | (c & 0x3F));
			return 4;
		}
		else if (c <= 0x3FFFFFF)  // 1111 10XX - five bytes
		{
			dst += (char) (0xF8 | (c >> 24));
			dst += (char) (0x80 | (c >> 18));
			dst += (char) (0x80 | ((c >> 12) & 0x3F));
			dst += (char) (0x80 | ((c >> 6) & 0x3F));
			dst += (char) (0x80 | (c & 0x3F));
			return 5;
		}
		else if (c <= 0x7FFFFFFF)  // 1111 110X - six bytes
		{
			dst += (char) (0xFC | (c >> 30));
			dst += (char) (0x80 | ((c >> 24) & 0x3F));
			dst += (char) (0x80 | ((c >> 18) & 0x3F));
			dst += (char) (0x80 | ((c >> 12) & 0x3F));
			dst += (char) (0x80 | ((c >> 6) & 0x3F));
			dst += (char) (0x80 | (c & 0x3F));
			return 6;
		}
		else {
			return 0; // Error
		}
	}

	std::string quote(char c) {
		if (c == 0)    { return "<eof>";   }
		if (c == ' ')  { return "<space>"; }
		if (c == '\n') { return "'\\n'";   }
		if (c == '\t') { return "'\\t'";   }
		if (c == '\r') { return "'\\r'";   }
		if (c == '\b') { return "'\\b'";   }
		return (std::string)"'" + c + "'";
	}

	struct State
	{
		const char* ptr;
		unsigned    line_nr;
		const char* line_start;
	};

	struct Parser
	{
		Parser(const char* str, const FormatOptions& options, DocInfo_SP doc, ParseInfo& info);

		bool skip_white(Comments* out_comments, int& out_indentation, bool break_on_newline);

		bool skip_white_ignore_comments() {
			int indentation;
			return skip_white(nullptr, indentation, false);
		}
		bool skip_pre_white(Config* config, int& out_indentation) {
			Comments comments;
			bool did_skip = skip_white(&comments, out_indentation, false);
			if (!comments.empty()) {
				append(config->comments().prefix, std::move(comments));
			}
			return did_skip;
		}
		bool skip_post_white(Config* config) {
			Comments comments;
			int indentation;
			bool did_skip = skip_white(&comments, indentation, true);
			if (!comments.empty()) {
				append(config->comments().postfix, std::move(comments));
			}
			return did_skip;
		}

		Config top_level();
		void parse_value(Config& out, bool* out_did_skip_postwhites);
		void parse_array(Config& dst);
		void parse_array_contents(Config& dst);
		void parse_object(Config& dst);
		void parse_object_contents(Config& dst);
		void parse_int(Config& out);
		void parse_float(Config& out);
		void parse_finite_number(Config& dst);
		std::string parse_string();
		uint64_t parse_hex(int count);
		void parse_macro(Config& dst);

		void tag(Config& var)
		{
			var.tag(_doc, _line_nr, column());
		}

		State get_state() const
		{
			return { _ptr, _line_nr, _line_start };
		}

		void set_state(State s) {
			_ptr        = s.ptr;
			_line_nr    = s.line_nr;
			_line_start = s.line_start;
		}

		unsigned column() const
		{
			return (unsigned)(_ptr - _line_start + 1);
		}

		const char* start_of_line() const
		{
			return _line_start;
		}

		const char* end_of_line() const
		{
			const char* p = _ptr;
			while (*p && *p != '\r' && *p != '\n') {
				++p;
			}
			return p;
		}

		void throw_error(const std::string& desc) CONFIGURU_NORETURN {
			const char* sol = start_of_line();
			const char* eol = end_of_line();
			std::string orientation;
			for (const char* p = sol; p != eol; ++p) {
				if (*p == '\t') {
					orientation += "    ";
				} else {
					orientation.push_back(*p);
				}
			}

			orientation += "\n";
			for (const char* p = sol; p != _ptr; ++p) {
				if (*p == '\t') {
					orientation += "    ";
				} else {
					orientation.push_back(' ');
				}
			}
			orientation += "^";

			throw ParseError(_doc, _line_nr, column(), desc + "\n" + orientation);
		}

		void throw_indentation_error(int found_tabs, int expected_tabs) {
			if (_options.enforce_indentation) {
				char buff[128];
				snprintf(buff, sizeof(buff), "Bad indentation: expected %d tabs, found %d", found_tabs, expected_tabs);
				throw_error(buff);
			}
		}

		void parse_assert(bool b, const char* error_msg) {
			if (!b) {
				throw_error(error_msg);
			}
		}

		void parse_assert(bool b, const char* error_msg, const State& error_state) {
			if (!b) {
				set_state(error_state);
				throw_error(error_msg);
			}
		}

		void swallow(char c) {
			if (_ptr[0] == c) {
				_ptr += 1;
			} else {
				throw_error("Expected " + quote(c));
			}
		}

		bool try_swallow(const char* str) {
			auto n = strlen(str);
			if (strncmp(str, _ptr, n) == 0) {
				_ptr += n;
				return true;
			} else {
				return false;
			}
		}

		void swallow(const char* str, const char* error_msg) {
			parse_assert(try_swallow(str), error_msg);
		}

		bool is_reserved_identifier(const char* ptr)
		{
			if (strncmp(ptr, "true", 4)==0 || strncmp(ptr, "null", 4)==0) {
				return !IDENT_CHARS[(uint8_t)ptr[4]];
			} else if (strncmp(ptr, "false", 5)==0) {
				return !IDENT_CHARS[(uint8_t)ptr[5]];
			} else {
				return false;
			}
		}

	private:
		bool IDENT_STARTERS[256] = { 0 };
		bool IDENT_CHARS[256]    = { 0 };

	private:
		FormatOptions _options;
		DocInfo_SP   _doc;
		ParseInfo&   _info;

		const char*  _ptr;
		unsigned     _line_nr;
		const char*  _line_start;
		int          _indentation = 0; // Expected number of tabs between a \n and the next key/value
	};

	// --------------------------------------------

	// Sets an inclusive range
	void set_range(bool lookup[256], char a, char b)
	{
		for (char c=a; c<=b; ++c) {
			lookup[(uint8_t)c] = true;
		}
	}

	Parser::Parser(const char* str, const FormatOptions& options, DocInfo_SP doc, ParseInfo& info) : _doc(doc), _info(info)
	{
		_options    = options;
		_line_nr    = 1;
		_ptr        = str;
		_line_start = str;

		IDENT_STARTERS[(uint8_t)'_'] = true;
		set_range(IDENT_STARTERS, 'a', 'z');
		set_range(IDENT_STARTERS, 'A', 'Z');

		IDENT_CHARS[(uint8_t)'_'] = true;
		set_range(IDENT_CHARS, 'a', 'z');
		set_range(IDENT_CHARS, 'A', 'Z');
		set_range(IDENT_CHARS, '0', '9');

		CONFIGURU_ASSERT(_options.indentation != "" || !_options.enforce_indentation);
	}

	// Returns true if we did skip white-space.
	// out_indentation is the depth of indentation on the last line we did skip on.
	// iff out_indentation is -1 there is a non-tab on the last line.
	bool Parser::skip_white(Comments* out_comments, int& out_indentation, bool break_on_newline)
	{
		auto start_ptr = _ptr;
		out_indentation = 0;
		bool found_newline = false;

		const std::string& indentation = _options.indentation;

		for (;;) {
			if (_ptr[0] == '\n') {
				// Unix style newline
				_ptr += 1;
				_line_nr += 1;
				_line_start = _ptr;
				out_indentation = 0;
				if (break_on_newline) { return true; }
				found_newline = true;
			}
			else if (_ptr[0] == '\r') {
				// CR-LF - windows style newline
				parse_assert(_ptr[1] == '\n', "CR with no LF. \\r only allowed before \\n."); // TODO: this is OK in JSON.
				_ptr += 2;
				_line_nr += 1;
				_line_start = _ptr;
				out_indentation = 0;
				if (break_on_newline) { return true; }
				found_newline = true;
			}
			else if (indentation != "" &&
					 strncmp(_ptr, indentation.c_str(), indentation.size()) == 0)
			{
				_ptr += indentation.size();
				if (_options.enforce_indentation && indentation == "\t") {
					parse_assert(out_indentation != -1, "Tabs should only occur on the start of a line!");
				}
				++out_indentation;
			}
			else if (_ptr[0] == '\t') {
				++_ptr;
				if (_options.enforce_indentation) {
					parse_assert(out_indentation != -1, "Tabs should only occur on the start of a line!");
				}
				++out_indentation;
			}
			else if (_ptr[0] == ' ') {
				if (found_newline && _options.enforce_indentation) {
					if (indentation == "\t") {
						throw_error("Found a space at beginning of a line. Indentation must be done using tabs!");
					} else {
						throw_error("Indentation should be a multiple of " + std::to_string(indentation.size()) + " spaces.");
					}
				}
				++_ptr;
				out_indentation = -1;
			}
			else if (_ptr[0] == '/' && _ptr[1] == '/') {
				parse_assert(_options.single_line_comments, "Single line comments forbidden.");
				// Single line comment
				auto start = _ptr;
				_ptr += 2;
				while (_ptr[0] && _ptr[0] != '\n') {
					_ptr += 1;
				}
				if (out_comments) { out_comments->emplace_back(start, _ptr - start); }
				out_indentation = 0;
				if (break_on_newline) { return true; }
			}
			else if (_ptr[0] == '/' && _ptr[1] == '*') {
				parse_assert(_options.block_comments, "Block comments forbidden.");
				// Multi-line comment
				auto state = get_state(); // So we can point out the start if there's an error
				_ptr += 2;
				unsigned nesting = 1; // We allow nested /**/ comments
				do
				{
					if (_ptr[0]==0) {
						set_state(state);
						throw_error("Non-ending /* comment");
					}
					else if (_ptr[0]=='/' && _ptr[1]=='*') {
						_ptr += 2;
						parse_assert(_options.nesting_block_comments, "Nesting comments (/* /* */ */) forbidden.");
						nesting += 1;
					}
					else if (_ptr[0]=='*' && _ptr[1]=='/') {
						_ptr += 2;
						nesting -= 1;
					}
					else if (_ptr[0] == '\n') {
						_ptr += 1;
						_line_nr += 1;
						_line_start = _ptr;
					} else {
						_ptr += 1;
					}
				} while (nesting > 0);
				if (out_comments) { out_comments->emplace_back(state.ptr, _ptr - state.ptr); }
				out_indentation = -1;
				if (break_on_newline) { return true; }
			}
			else {
				break;
			}
		}

		if (start_ptr == _ptr) {
			out_indentation = -1;
			return false;
		} else {
			return true;
		}
	}

	/*
	The top-level can be any value, OR the innerds of an object:
	foo = 1
	"bar": 2
	*/
	Config Parser::top_level()
	{
		bool is_object = false;

		if (_options.implicit_top_object)
		{
			auto state = get_state();
			skip_white_ignore_comments();

			if (IDENT_STARTERS[(uint8_t)_ptr[0]] && !is_reserved_identifier(_ptr)) {
				is_object = true;
			} else if (_ptr[0] == '"' || _ptr[0] == '@') {
				parse_string();
				skip_white_ignore_comments();
				is_object = (_ptr[0] == ':' || _ptr[0] == '=');
			}

			set_state(state); // restore
		}

		Config ret;
		tag(ret);

		if (is_object) {
			parse_object_contents(ret);
		} else {
			parse_array_contents(ret);
			parse_assert(ret.array_size() <= 1 || _options.implicit_top_array, "Multiple values not allowed without enclosing []");
		}

		skip_post_white(&ret);

		parse_assert(_ptr[0] == 0, "Expected EoF");

		if (!is_object && ret.array_size() == 0) {
			if (_options.empty_file) {
				auto empty_object = Config::object();
				if (ret.has_comments()) {
					empty_object.comments() = std::move(ret.comments());
				}
				return empty_object;
			} else {
				throw_error("Empty file");
			}
		}

		if (!is_object && ret.array_size() == 1) {
			// A single value - not an array after all:
			Config first( std::move(ret[0]) );
			if (ret.has_comments()) {
				first.comments().append(std::move(ret.comments()));
			}
			return first;
		}

		return ret;
	}

	void Parser::parse_value(Config& dst, bool* out_did_skip_postwhites)
	{
		int line_indentation;
		skip_pre_white(&dst, line_indentation);
		tag(dst);

		if (line_indentation >= 0 && _indentation - 1 != line_indentation) {
			throw_indentation_error(_indentation - 1, line_indentation);
		}

		if (_ptr[0] == '"' || _ptr[0] == '@') {
			dst = parse_string();
		}
		else if (_ptr[0] == 'n') {
			parse_assert(_ptr[1]=='u' && _ptr[2]=='l' && _ptr[3]=='l', "Expected 'null'");
			parse_assert(!IDENT_CHARS[(uint8_t)_ptr[4]], "Expected 'null'");
			_ptr += 4;
			dst = nullptr;
		}
		else if (_ptr[0] == 't') {
			parse_assert(_ptr[1]=='r' && _ptr[2]=='u' && _ptr[3]=='e', "Expected 'true'");
			parse_assert(!IDENT_CHARS[(uint8_t)_ptr[4]], "Expected 'true'");
			_ptr += 4;
			dst = true;
		}
		else if (_ptr[0] == 'f') {
			parse_assert(_ptr[1]=='a' && _ptr[2]=='l' && _ptr[3]=='s' && _ptr[4]=='e', "Expected 'false'");
			parse_assert(!IDENT_CHARS[(uint8_t)_ptr[5]], "Expected 'false'");
			_ptr += 5;
			dst = false;
		}
		else if (_ptr[0] == '{') {
			parse_object(dst);
		}
		else if (_ptr[0] == '[') {
			parse_array(dst);
		}
		else if (_ptr[0] == '#') {
			parse_macro(dst);
		}
		else if (_ptr[0] == '+' || _ptr[0] == '-' || _ptr[0] == '.' || ('0' <= _ptr[0] && _ptr[0] <= '9')) {
			// Some kind of number:

			if (_ptr[0] == '-' && _ptr[1] == 'i' && _ptr[2]=='n' && _ptr[3]=='f') {
				parse_assert(!IDENT_CHARS[(uint8_t)_ptr[4]], "Expected -inf");
				parse_assert(_options.inf, "infinity forbidden.");
				_ptr += 4;
				dst = -std::numeric_limits<double>::infinity();
			}
			else if (_ptr[0] == '+' && _ptr[1] == 'i' && _ptr[2]=='n' && _ptr[3]=='f') {
				parse_assert(!IDENT_CHARS[(uint8_t)_ptr[4]], "Expected +inf");
				parse_assert(_options.inf, "infinity forbidden.");
				_ptr += 4;
				dst = std::numeric_limits<double>::infinity();
			}
			else if (_ptr[0] == '+' && _ptr[1] == 'N' && _ptr[2]=='a' && _ptr[3]=='N') {
				parse_assert(!IDENT_CHARS[(uint8_t)_ptr[4]], "Expected +NaN");
				parse_assert(_options.nan, "NaN (Not a Number) forbidden.");
				_ptr += 4;
				dst = std::numeric_limits<double>::quiet_NaN();
			} else {
				parse_finite_number(dst);
			}
		} else {
			throw_error("Expected value");
		}

		*out_did_skip_postwhites = skip_post_white(&dst);
	}

	void Parser::parse_array(Config& array)
	{
		auto state = get_state();

		swallow('[');

		_indentation += 1;
		parse_array_contents(array);
		_indentation -= 1;

		if (_ptr[0] == ']') {
			_ptr += 1;
		} else {
			set_state(state);
			throw_error("Non-terminated array");
		}
	}

	void Parser::parse_array_contents(Config& array)
	{
		array.make_array();

		Comments next_prefix_comments;

		for (;;)
		{
			Config value;
			if (!next_prefix_comments.empty()) {
				std::swap(value.comments().prefix, next_prefix_comments);
			}
			int line_indentation;
			skip_pre_white(&value, line_indentation);

			if (_ptr[0] == ']') {
				if (line_indentation >= 0 && _indentation - 1 != line_indentation) {
					throw_indentation_error(_indentation - 1, line_indentation);
				}
				if (value.has_comments()) {
					array.comments().pre_end_brace = value.comments().prefix;
				}
				break;
			}

			if (!_ptr[0]) {
				if (value.has_comments()) {
					array.comments().pre_end_brace = value.comments().prefix;
				}
				break;
			}

			if (line_indentation >= 0 && _indentation != line_indentation) {
				throw_indentation_error(_indentation, line_indentation);
			}

			if (IDENT_STARTERS[(uint8_t)_ptr[0]] && !is_reserved_identifier(_ptr)) {
				throw_error("Found identifier; expected value. Did you mean to use a {object} rather than a [array]?");
			}

			bool has_separator;
			parse_value(value, &has_separator);
			int ignore;
			skip_white(&next_prefix_comments, ignore, false);

			auto comma_state = get_state();
			bool has_comma = _ptr[0] == ',';

			if (has_comma) {
				_ptr += 1;
				skip_post_white(&value);
				has_separator = true;
			}

			array.push_back(std::move(value));

			bool is_last_element = !_ptr[0] || _ptr[0] == ']';

			if (is_last_element) {
				parse_assert(!has_comma || _options.array_trailing_comma,
					"Trailing comma forbidden.", comma_state);
			} else {
				if (_options.array_omit_comma) {
					parse_assert(has_separator, "Expected a space, newline, comma or ]");
				} else {
					parse_assert(has_comma, "Expected a comma or ]");
				}
			}
		}
	}

	void Parser::parse_object(Config& object)
	{
		auto state = get_state();

		swallow('{');

		_indentation += 1;
		parse_object_contents(object);
		_indentation -= 1;

		if (_ptr[0] == '}') {
			_ptr += 1;
		} else {
			set_state(state);
			throw_error("Non-terminated object");
		}
	}

	void Parser::parse_object_contents(Config& object)
	{
		object.make_object();

		Comments next_prefix_comments;

		for (;;)
		{
			Config value;
			if (!next_prefix_comments.empty()) {
				std::swap(value.comments().prefix, next_prefix_comments);
			}
			int line_indentation;
			skip_pre_white(&value, line_indentation);

			if (_ptr[0] == '}') {
				if (line_indentation >= 0 && _indentation - 1 != line_indentation) {
					throw_indentation_error(_indentation - 1, line_indentation);
				}
				if (value.has_comments()) {
					object.comments().pre_end_brace = value.comments().prefix;
				}
				break;
			}

			if (!_ptr[0]) {
				if (value.has_comments()) {
					object.comments().pre_end_brace = value.comments().prefix;
				}
				break;
			}

			if (line_indentation >= 0 && _indentation != line_indentation) {
				throw_indentation_error(_indentation, line_indentation);
			}

			auto pre_key_state = get_state();
			std::string key;

			if (IDENT_STARTERS[(uint8_t)_ptr[0]] && !is_reserved_identifier(_ptr)) {
				parse_assert(_options.identifiers_keys, "You need to surround keys with quotes");
				while (IDENT_CHARS[(uint8_t)_ptr[0]]) {
					key += _ptr[0];
					_ptr += 1;
				}
			}
			else if (_ptr[0] == '"' || _ptr[0] == '@') {
				key = parse_string();
			} else {
				throw_error("Object key expected (either an identifier or a quoted string), got " + quote(_ptr[0]));
			}

			if (!_options.object_duplicate_keys && object.has_key(key)) {
				set_state(pre_key_state);
				throw_error("Duplicate key: \"" + key + "\". Already set at " + object[key].where());
			}

			bool space_after_key = skip_white_ignore_comments();

			if (_ptr[0] == ':' || (_options.object_separator_equal && _ptr[0] == '=')) {
				parse_assert(_options.allow_space_before_colon || _ptr[0] != ':' || !space_after_key, "No space allowed before colon");
				_ptr += 1;
				skip_white_ignore_comments();
			} else if (_options.omit_colon_before_object && (_ptr[0] == '{' || _ptr[0] == '#')) {
				// Ok to ommit : in this case
			} else {
				if (_options.object_separator_equal && _options.omit_colon_before_object) {
					throw_error("Expected one of '=', ':', '{' or '#' after object key");
				} else {
					throw_error("Expected : after object key");
				}
			}

			bool has_separator;
			parse_value(value, &has_separator);
			int ignore;
			skip_white(&next_prefix_comments, ignore, false);

			auto comma_state = get_state();
			bool has_comma = _ptr[0] == ',';

			if (has_comma) {
				_ptr += 1;
				skip_post_white(&value);
				has_separator = true;
			}

			object.insert_or_assign(key, std::move(value));

			bool is_last_element = !_ptr[0] || _ptr[0] == '}';

			if (is_last_element) {
				parse_assert(!has_comma || _options.object_trailing_comma,
					"Trailing comma forbidden.", comma_state);
			} else {
				if (_options.object_omit_comma) {
					parse_assert(has_separator, "Expected a space, newline, comma or }");
				} else {
					parse_assert(has_comma, "Expected a comma or }");
				}
			}
		}
	}

	void Parser::parse_int(Config& out)
	{
		const auto start = _ptr;
		const auto result = strtoll(start, (char**)&_ptr, 10);
		parse_assert(start < _ptr, "Invalid integer");
		parse_assert(start[0] != '0' || result == 0, "Integer may not start with a zero");
		out = result;
	}

	void Parser::parse_float(Config& out)
	{
		const auto start = _ptr;
		const double result = strtod(start, (char**)&_ptr);
		parse_assert(start < _ptr, "Invalid number");
		out = result;
	}

	void Parser::parse_finite_number(Config& out)
	{
		const auto pre_sign = _ptr;
		int sign = +1;

		if (_ptr[0] == '+') {
			parse_assert(_options.unary_plus, "Prefixing numbers with + is forbidden.");
			_ptr += 1;
			skip_white_ignore_comments();
		}
		if (_ptr[0] == '-') {
			_ptr += 1;
			skip_white_ignore_comments();
			sign = -1;
		}

		parse_assert(_ptr[0] != '+' && _ptr[0] != '-', "Duplicate sign");

		// Check if it's an integer:
		if (_ptr[0] == '0' && _ptr[1] == 'x') {
			parse_assert(_options.hexadecimal_integers, "Hexadecimal numbers forbidden.");
			_ptr += 2;
			auto start = _ptr;
			out = sign * (int64_t)strtoull(start, (char**)&_ptr, 16);
			parse_assert(start < _ptr, "Missing hexaxdecimal digits after 0x");
			return;
		}

		if (_ptr[0] == '0' && _ptr[1] == 'b') {
			parse_assert(_options.binary_integers, "Binary numbers forbidden.");
			_ptr += 2;
			auto start = _ptr;
			out = sign * (int64_t)strtoull(start, (char**)&_ptr, 2);
			parse_assert(start < _ptr, "Missing binary digits after 0b");
			return;
		}

		const char* p = _ptr;

		while ('0' <= *p && *p <= '9') {
			p += 1;
		}

		if (*p == '.' || *p == 'e' || *p == 'E') {
			_ptr = pre_sign;
			return parse_float(out);
		}

		// It looks like an integer - but it may be too long to represent as one!
		const auto MAX_INT_STR = (sign == +1 ? "9223372036854775807" : "9223372036854775808");

		const size_t length = p - _ptr;

		if (length < 19) {
			_ptr = pre_sign;
			return parse_int(out);
		}

		if (length > 19) {
			_ptr = pre_sign;
			return parse_float(out); // Uncommon case optimization
		}

		// Compare fast:
		for (int i = 0; i < 19; ++i)
		{
			if (_ptr[i] > MAX_INT_STR[i]) {
				_ptr = pre_sign;
				return parse_float(out);
			}
			if (_ptr[i] < MAX_INT_STR[i]) {
				_ptr = pre_sign;
				return parse_int(out);
			}
		}
		_ptr = pre_sign;
		return parse_int(out); // Exactly max int
	}

	std::string Parser::parse_string()
	{
		auto state = get_state();

		if (_ptr[0] == '@') {
			// C# style verbatim string - everything until the next " except "" which is ":
			parse_assert(_options.str_csharp_verbatim, "C# @-style verbatim strings forbidden.");
			_ptr += 1;
			swallow('"');

			std::string str;

			for (;;) {
				if (_ptr[0] == 0) {
					set_state(state);
					throw_error("Unterminated verbatim string");
				}
				else if (_ptr[0] == '\n') {
					throw_error("Newline in verbatim string");
				}
				else if (_ptr[0] == '"' && _ptr[1] == '"') {
					// Escaped quote
					_ptr += 2;
					str += '"';
				}
				else if (_ptr[0] == '"') {
					_ptr += 1;
					return str;
				}
				else {
					str += _ptr[0];
					_ptr += 1;
				}
			}
		}

		parse_assert(_ptr[0] == '"', "Quote (\") expected");

		if (_ptr[1] == '"' && _ptr[2] == '"') {
			// Multiline string - everything until the next """:
			parse_assert(_options.str_python_multiline, "Python \"\"\"-style multiline strings forbidden.");
			_ptr += 3;
			const char* start = _ptr;
			for (;;) {
				if (_ptr[0]==0 || _ptr[1]==0 || _ptr[2]==0) {
					set_state(state);
					throw_error("Unterminated multiline string");
				}

				if (_ptr[0] == '"' && _ptr[1] == '"' && _ptr[2] == '"' && _ptr[3] != '"') {
					std::string str(start, _ptr);
					_ptr += 3;
					return str;
				}

				if (_ptr[0] == '\n') {
					_ptr += 1;
					_line_nr += 1;
					_line_start = _ptr;
				} else {
					_ptr += 1;
				}
			}
		} else {
			// Normal string
			_ptr += 1; // Swallow quote

			std::string str;
			for (;;) {
				if (_ptr[0] == 0) {
					set_state(state);
					throw_error("Unterminated string");
				}
				if (_ptr[0] == '"') {
					_ptr += 1;
					return str;
				}
				if (_ptr[0] == '\n') {
					throw_error("Newline in string");
				}
				if (_ptr[0] == '\t') {
					parse_assert(_options.str_allow_tab, "Un-escaped tab not allowed in string");
				}

				if (_ptr[0] == '\\') {
					// Escape sequence
					_ptr += 1;

					if (_ptr[0] == '"') {
						str += '"';
						_ptr += 1;
					} else if (_ptr[0] == '\\') {
						str += '\\';
						_ptr += 1;
					} else if (_ptr[0] == '/') {
						str += '/';
						_ptr += 1;
					} else if (_ptr[0] == 'b') {
						str += '\b';
						_ptr += 1;
					} else if (_ptr[0] == 'f') {
						str += '\f';
						_ptr += 1;
					} else if (_ptr[0] == 'n') {
						str += '\n';
						_ptr += 1;
					} else if (_ptr[0] == 'r') {
						str += '\r';
						_ptr += 1;
					} else if (_ptr[0] == 't') {
						str += '\t';
						_ptr += 1;
					} else if (_ptr[0] == 'u') {
						// Four hexadecimal characters
						_ptr += 1;
						uint64_t codepoint = parse_hex(4);

						if (0xD800 <= codepoint and codepoint <= 0xDBFF)
						{
							// surrogate pair
							parse_assert(_ptr[0] == '\\' && _ptr[1] == 'u',
										 "Missing second unicode surrogate.");
							_ptr += 2;
							uint64_t codepoint2 = parse_hex(4);
							parse_assert(0xDC00 <= codepoint2 && codepoint2 <= 0xDFFF, "Invalid second unicode surrogate");
							codepoint = (codepoint << 10) + codepoint2 - 0x35FDC00;
						}

						auto num_bytes_written = encode_utf8(str, codepoint);
						parse_assert(num_bytes_written > 0, "Bad unicode codepoint");
					} else if (_ptr[0] == 'U') {
						// Eight hexadecimal characters
						parse_assert(_options.str_32bit_unicode, "\\U 32 bit unicodes forbidden.");
						_ptr += 1;
						uint64_t unicode = parse_hex(8);
						auto num_bytes_written = encode_utf8(str, unicode);
						parse_assert(num_bytes_written > 0, "Bad unicode codepoint");
					} else {
						throw_error("Unknown escape character " + quote(_ptr[0]));
					}
				} else {
					str += _ptr[0];
					_ptr += 1;
				}
			}
		}
	}

	uint64_t Parser::parse_hex(int count)
	{
		uint64_t ret = 0;
		for (int i=0; i<count; ++i) {
			ret *= 16;
			char c = _ptr[i];
			if ('0' <= c && c <= '9') {
				ret += c - '0';
			} else if ('a' <= c && c <= 'f') {
				ret += 10 + c - 'a';
			} else if ('A' <= c && c <= 'F') {
				ret += 10 + c - 'A';
			} else {
				throw_error("Expected hexadecimal digit, got " + quote(_ptr[0]));
			}
		}
		_ptr += count;
		return ret;
	}

	void Parser::parse_macro(Config& dst)
	{
		parse_assert(_options.allow_macro, "#macros forbidden.");

		swallow("#include", "Expected '#include'");
		skip_white_ignore_comments();

		bool absolute;
		char terminator;

		if (_ptr[0] == '"') {
			absolute = false;
			terminator = '"';
		} else if (_ptr[0] == '<') {
			absolute = true;
			terminator = '>';
		} else {
			throw_error("Expected \" or <");
		}

		auto state = get_state();
		_ptr += 1;
		auto start = _ptr;
		std::string path;
		for (;;) {
			if (_ptr[0] == 0) {
				set_state(state);
				throw_error("Unterminated include path");
			} else if (_ptr[0] == terminator) {
				path = std::string(start, _ptr-start);
				_ptr += 1;
				break;
			} else if (_ptr[0] == '\n') {
				throw_error("Newline in string");
			} else {
				_ptr += 1;
			}
		}

		if (!absolute) {
			auto my_path = _doc->filename;
			auto pos = my_path.find_last_of('/');
			if (pos != std::string::npos) {
				auto my_dir = my_path.substr(0, pos+1);
				path = my_dir + path;
			}
		}

		auto it = _info.parsed_files.find(path);
		if (it == _info.parsed_files.end()) {
			auto child_doc = std::make_shared<DocInfo>(path);
			child_doc->includers.emplace_back(_doc, _line_nr);
			dst = parse_file(path.c_str(), _options, child_doc, _info);
			_info.parsed_files[path] = dst;
		} else {
			auto child_doc = it->second.doc();
			child_doc->includers.emplace_back(_doc, _line_nr);
			dst = it->second;
		}
	}

	// ----------------------------------------------------------------------------------------

	Config parse_string(const char* str, const FormatOptions& options, DocInfo_SP doc, ParseInfo& info)
	{
		Parser p(str, options, doc, info);
		return p.top_level();
	}

	Config parse_string(const char* str, const FormatOptions& options, const char* name)
	{
		ParseInfo info;
		return parse_string(str, options, std::make_shared<DocInfo>(name), info);
	}

	std::string read_text_file(const char* path)
	{
		FILE* fp = fopen(path, "rb");
		if (fp == nullptr) {
			CONFIGURU_ONERROR((std::string)"Failed to open '" + path + "' for reading: " + strerror(errno));
		}
		std::string contents;
		fseek(fp, 0, SEEK_END);
		contents.resize(ftell(fp));
		rewind(fp);
		auto num_read = fread(&contents[0], 1, contents.size(), fp);
		fclose(fp);
		if (num_read != contents.size()) {
			CONFIGURU_ONERROR((std::string)"Failed to read from '" + path + "': " + strerror(errno));
		}
		return contents;
	}

	Config parse_file(const std::string& path, const FormatOptions& options, DocInfo_SP doc, ParseInfo& info)
	{
		// auto file = util::FILEWrapper::read_text_file(path);
		auto file = read_text_file(path.c_str());
		return parse_string(file.c_str(), options, doc, info);
	}

	Config parse_file(const std::string& path, const FormatOptions& options)
	{
		ParseInfo info;
		return parse_file(path, options, std::make_shared<DocInfo>(path), info);
	}
}

// ----------------------------------------------------------------------------
// Yb        dP 88""Yb 88 888888 888888 88""Yb
//  Yb  db  dP  88__dP 88   88   88__   88__dP
//   YbdPYbdP   88"Yb  88   88   88""   88"Yb
//    YP  YP    88  Yb 88   88   888888 88  Yb

#include <cstdlib>  // strtod

namespace configuru
{
	bool is_identifier(const char* p)
	{
		if (*p == '_'
			 || ('a' <= *p && *p <= 'z')
			 || ('A' <= *p && *p <= 'Z'))
		{
			++p;
			while (*p) {
				if (*p == '_'
					 || ('a' <= *p && *p <= 'z')
					 || ('A' <= *p && *p <= 'Z')
					 || ('0' <= *p && *p <= '9'))
				{
					++p;
				} else {
					return false;
				}
			}
			return true;
		} else {
			return false;
		}
	}

	bool has_pre_end_brace_comments(const Config& cfg)
	{
		return cfg.has_comments() && !cfg.comments().pre_end_brace.empty();
	}

	struct Writer
	{
		DocInfo_SP    _doc;
		FormatOptions _options;
		std::string   _out;

		void write_indent(unsigned indent)
		{
			if (_options.compact()) { return; }
			for (unsigned i=0; i<indent; ++i) {
				_out += _options.indentation;
			}
		}

		void write_prefix_comments(unsigned indent, const Comments& comments)
		{
			if (!_options.write_comments) { return; }
			if (!comments.empty()) {
				_out += "\n";
				for (auto&& c : comments) {
					write_indent(indent);
					_out += c;
					_out.push_back('\n');
				}
			}
		}

		void write_prefix_comments(unsigned indent, const Config& cfg)
		{
			if (!_options.write_comments) { return; }
			if (cfg.has_comments()) {
				write_prefix_comments(indent, cfg.comments().prefix);
			}
		}

		void write_postfix_comments(unsigned indent, const Comments& comments)
		{
			if (!_options.write_comments) { return; }
			(void)indent; // TODO: reindent comments
			for (auto&& c : comments) {
				_out += " ";
				_out += c;
			}
		}

		void write_pre_brace_comments(unsigned indent, const Comments& comments)
		{
			write_prefix_comments(indent, comments);
		}

		void write_value(unsigned indent, const Config& config,
							  bool write_prefix, bool write_postfix)
		{
			if (_options.allow_macro && config.doc() && config.doc() != _doc) {
				dump_file(config.doc()->filename, config, _options);
				_out += "#include <";
				_out += config.doc()->filename;
				_out += ">";
				return;
			}

			if (write_prefix) {
				write_prefix_comments(indent, config);
			}

			if (config.is_null()) {
				_out += "null";
			} else if (config.is_bool()) {
				_out += ((bool)config ? "true" : "false");
			} else if (config.is_int()) {
				char temp_buff[64];
				snprintf(temp_buff, sizeof(temp_buff), "%lld", (long long)config);
				_out += temp_buff;
			} else if (config.is_float()) {
				write_number( (double)config );
			} else if (config.is_string()) {
				write_string(config.as_string());
			} else if (config.is_array()) {
				if (config.array_size() == 0 && !has_pre_end_brace_comments(config)) {
					if (_options.compact()) {
						_out += "[]";
					} else {
						_out += "[ ]";
					}
				} else if (_options.compact() || is_simple_array(config)) {
					_out += "[";
					if (!_options.compact()) {
						_out += " ";
					}
					auto&& array = config.as_array();
					for (size_t i = 0; i < array.size(); ++i) {
						write_value(indent + 1, array[i], false, true);
						if (_options.compact()) {
							if (i + 1 < array.size()) {
								_out += ",";
							}
						} else if (_options.array_omit_comma || i + 1 == array.size()) {
							_out += " ";
						} else {
							_out += ", ";
						}
					}
					write_pre_brace_comments(indent + 1, config.comments().pre_end_brace);
					_out += "]";
				} else {
					_out += "[\n";
					auto&& array = config.as_array();
					for (size_t i = 0; i < array.size(); ++i) {
						write_prefix_comments(indent + 1, array[i]);
						write_indent(indent + 1);
						write_value(indent + 1, array[i], false, true);
						if (_options.array_omit_comma || i + 1 == array.size()) {
							_out += "\n";
						} else {
							_out += ",\n";
						}
					}
					write_pre_brace_comments(indent + 1, config.comments().pre_end_brace);
					write_indent(indent);
					_out += "]";
				}
			} else if (config.is_object()) {
				if (config.object_size() == 0 && !has_pre_end_brace_comments(config)) {
					if (_options.compact()) {
						_out += "{}";
					} else {
						_out += "{ }";
					}
				} else {
					if (_options.compact()) {
						_out += "{";
					} else {
						_out += "{\n";
					}
					write_object_contents(indent + 1, config);
					write_indent(indent);
					_out += "}";
				}
			} else {
				if (_options.write_uninitialized) {
					_out += "UNINITIALIZED";
				} else {
					CONFIGURU_ONERROR("Failed to serialize uninitialized Config");
				}
			}

			if (write_postfix) {
				write_postfix_comments(indent, config.comments().postfix);
			}
		}

		void write_object_contents(unsigned indent, const Config& config)
		{
			// Write in same order as input:
			auto&& object = config.as_object()._impl;

			using ObjIterator = Config::ConfigObjectImpl::const_iterator;
			std::vector<ObjIterator> pairs;

			size_t longest_key = 0;
			for (auto it=object.begin(); it!=object.end(); ++it) {
				pairs.push_back(it);
				longest_key = std::max(longest_key, it->first.size());
			}

			if (_options.sort_keys) {
				std::sort(begin(pairs), end(pairs), [](const ObjIterator& a, const ObjIterator& b) {
					return a->first < b->first;
				});
			} else {
				std::sort(begin(pairs), end(pairs), [](const ObjIterator& a, const ObjIterator& b) {
					return a->second._nr < b->second._nr;
				});
			}

			size_t i = 0;
			for (auto&& it : pairs) {
				auto&& value = it->second._value;
				write_prefix_comments(indent, value);
				write_indent(indent);
				write_key(it->first);
				if (_options.compact()) {
					_out += ":";
				} else if (_options.omit_colon_before_object && value.is_object() && value.object_size() != 0) {
					_out += " ";
				} else {
					_out += ": ";
					for (size_t j=it->first.size(); j<longest_key; ++j) {
						_out += " ";
					}
				}
				write_value(indent, value, false, true);
				if (_options.compact()) {
					if (i + 1 < pairs.size()) {
						_out += ",";
					}
				} else if (_options.array_omit_comma || i + 1 == pairs.size()) {
					_out += "\n";
				} else {
					_out += ",\n";
				}
				i += 1;
			}

			write_pre_brace_comments(indent, config.comments().pre_end_brace);
		}

		void write_key(const std::string& str)
		{
			if (_options.identifiers_keys && is_identifier(str.c_str())) {
				_out += str;
			} else {
				write_string(str);
			}
		}

		void write_number(double val)
		{
			if (_options.distinct_floats && val == 0 && std::signbit(val)) {
				_out += "-0.0";
				return;
			}

			auto as_int = (long long)val;
			if ((double)as_int == val) {
				char temp_buff[64];
				snprintf(temp_buff, sizeof(temp_buff), "%lld", as_int);
				_out += temp_buff;
				if (_options.distinct_floats) {
					_out += ".0";
				}
				return;
			}

			if (std::isfinite(val)) {
				char temp_buff[64];

				auto as_float = (float)val;
				if ((double)as_float == val) {
					// It's actually a float!
					snprintf(temp_buff, sizeof(temp_buff), "%g", as_float);
					if (std::strtof(temp_buff, nullptr) == as_float) {

						_out += temp_buff;
					} else {
						snprintf(temp_buff, sizeof(temp_buff), "%.8g", as_float);
						_out += temp_buff;
					}
					return;
				}

				// Try single digit of precision (for denormals):
				snprintf(temp_buff, sizeof(temp_buff), "%.1g", val);
				if (std::strtod(temp_buff, nullptr) == val) {
					_out += temp_buff;
					return;
				}

				// Try default digits of precision:
				snprintf(temp_buff, sizeof(temp_buff), "%g", val);
				if (std::strtod(temp_buff, nullptr) == val) {
					_out += temp_buff;
					return;
				}

				// Try 16 digits of precision:
				snprintf(temp_buff, sizeof(temp_buff), "%.16g", val);
				if (std::strtod(temp_buff, nullptr) == val) {
					_out += temp_buff;
					return;
				}

				// Nope, full 17 digits needed:
				snprintf(temp_buff, sizeof(temp_buff), "%.17g", val);
				_out += temp_buff;
			} else if (val == +std::numeric_limits<double>::infinity()) {
				if (!_options.inf) {
					CONFIGURU_ONERROR("Can't encode infinity");
				}
				_out += "+inf";
			} else if (val == -std::numeric_limits<double>::infinity()) {
				if (!_options.inf) {
					CONFIGURU_ONERROR("Can't encode negative infinity");
				}
				_out += "-inf";
			} else {
				if (!_options.nan) {
					CONFIGURU_ONERROR("Can't encode NaN");
				}
				_out += "+NaN";
			}
		}

		void write_string(const std::string& str)
		{
			const size_t LONG_LINE = 240;

			if (!_options.str_python_multiline       ||
				str.find('\n') == std::string::npos ||
				str.length() < LONG_LINE            ||
				str.find("\"\"\"") != std::string::npos)
			{
				write_quoted_string(str);
			} else {
				write_verbatim_string(str);
			}
		}

		void write_hex_digit(unsigned num)
		{
			CONFIGURU_ASSERT(num < 16u);
			if (num < 10u) { _out.push_back(char('0' + num)); }
			else { _out.push_back(char('a' + num - 10)); }
		}

		void write_hex_16(uint16_t n)
		{
			write_hex_digit((n >> 12) & 0x0f);
			write_hex_digit((n >>  8) & 0x0f);
			write_hex_digit((n >>  4) & 0x0f);
			write_hex_digit((n >>  0) & 0x0f);
		}

		void write_unicode_16(uint16_t c)
		{
			_out += "\\u";
			write_hex_16(c);
		}

		void write_quoted_string(const std::string& str) {
			_out += '"';
			for (char c : str) {
				if      (c == '\\') { _out += "\\\\"; }
				else if (c == '\"') { _out += "\\\""; }
				//else if (c == '\'') { _out += "\\\'"; }
				else if (c == '\0') { _out += "\\0";  }
				else if (c == '\b') { _out += "\\b";  }
				else if (c == '\f') { _out += "\\f";  }
				else if (c == '\n') { _out += "\\n";  }
				else if (c == '\r') { _out += "\\r";  }
				else if (c == '\t') { _out += "\\t";  }
				else if (0 <= c && c < 0x20) { write_unicode_16(c); }
				else { _out.push_back(c); } // TODO: add option to escape unicode
			}
			_out.push_back('"');
		}

		void write_verbatim_string(const std::string& str)
		{
			_out += "\"\"\"";
			_out += str;
			_out += "\"\"\"";
		}

		bool is_simple(const Config& var)
		{
			if (var.is_array()  && var.array_size()  > 0) { return false; }
			if (var.is_object() && var.object_size() > 0) { return false; }
			if (_options.write_comments && var.has_comments()) { return false; }
			return true;
		}

		bool is_all_numbers(const Config& array)
		{
			for (auto& v: array.as_array()) {
				if (!v.is_number()) {
					return false;
				}
			}
			return true;
		}

		bool is_simple_array(const Config& array)
		{
			if (array.array_size() <= 16 && is_all_numbers(array)) {
				return true; // E.g., a 4x4 matrix
			}

			if (array.array_size() > 4) { return false; }
			size_t estimated_width = 0;
			for (auto& v: array.as_array()) {
				if (!is_simple(v)) {
					return false;
				}
				if (v.is_string()) {
					estimated_width += 2 + v.as_string().size();
				} else {
					estimated_width += 5;
				}
				estimated_width += 2;
			}
			return estimated_width < 60;
		}
	}; // struct Writer

	std::string dump_string(const Config& config, const FormatOptions& options)
	{
		Writer w;
		w._options = options;
		w._doc     = config.doc();

		if (options.implicit_top_object && config.is_object()) {
			w.write_object_contents(0, config);
		} else {
			w.write_value(0, config, true, true);
			w._out += "\n"; // Good form
		}

		return std::move(w._out);
	}

	static void write_text_file(const char* path, const std::string& data)
	{
		auto fp = fopen(path, "wb");
		if (fp == nullptr) {
			CONFIGURU_ONERROR((std::string)"Failed to open '" + path + "' for writing: " + strerror(errno));
		}
		auto num_bytes_written = fwrite(data.data(), 1, data.size(), fp);
		fclose(fp);
		if (num_bytes_written != data.size()) {
			CONFIGURU_ONERROR((std::string)"Failed to write to '" + path + "': " + strerror(errno));
		}
	}

	void dump_file(const std::string& path, const configuru::Config& config, const FormatOptions& options)
	{
		auto str = dump_string(config, options);
		write_text_file(path.c_str(), str);
	}
} // namespace configuru

// ----------------------------------------------------------------------------

#endif // CONFIGURU_IMPLEMENTATION
