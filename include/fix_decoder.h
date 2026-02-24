/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   include/fix_decoder.h
 * Description: Decoder interface for FIX messages.
 *
 * Copyright (C) 2026 Dieter J Kybelksties <github@kybelksties.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * @date: 2026-02-11
 * @author: Dieter J Kybelksties
 */

#ifndef FIXDECODER_FIX_DECODER_H_INCLUDED
#define FIXDECODER_FIX_DECODER_H_INCLUDED

#include "fix_dictionary.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fix {
    /**
     * @brief Represents a single decoded FIX field.
     */
    struct DecodedField {
        /** Numeric FIX tag (for example 35 for MsgType). */
        std::uint32_t tag = 0;
        /** Dictionary field name if known, otherwise empty. */
        std::string name;
        /** Dictionary type (for example STRING, INT, FLOAT), if known. */
        std::string type;
        /** Raw field value view into DecodedMessage::normalized_message. */
        std::string_view value;
        /** Typed value decoded from `value` using dictionary type metadata. */
        std::variant<std::monostate, bool, std::int64_t, float, double, std::string_view> typed_value;
    };

    /**
     * @brief Holds the parsed content of a decoded FIX message.
     */
    struct DecodedMessage {
        /** Value of tag 8 (BeginString), if present. */
        std::string begin_string;
        /** Value of tag 35 (MsgType), if present. */
        std::string msg_type;
        /** Normalized message storage that backs all field value string_views. */
        std::string normalized_message;
        /** All parsed fields in message order. */
        std::vector<DecodedField> fields;
        /** Structural validation status derived from dictionary members/components/groups. */
        bool structurally_valid = true;
        /** Human-readable validation errors when `structurally_valid` is `false`. */
        std::vector<std::string> validation_errors;
    };

    /**
     * @brief A node in a decoded FIX object graph.
     */
    struct DecodedObjectNode {
        using Value = std::variant<std::monostate, bool, std::int64_t, float, double, std::string_view>;

        /** Typed field value decoded via generated decoder maps. */
        Value value;
        /** Optional nested children for hierarchical decoding extensions. */
        std::unordered_map<std::uint32_t, DecodedObjectNode> children;
    };

    /**
     * @brief Lightweight lookup handle returned by `DecodedObject::operator[]`.
     */
    class DecodedObjectLookup {
    public:
        using Value = DecodedObjectNode::Value;

        /**
         * @brief Returns child lookup if present; otherwise falls back to root-level lookup.
         * @param tag Numeric FIX tag.
         * @return Lookup handle for chained access.
         */
        DecodedObjectLookup operator[](std::uint32_t tag) const;

        /**
         * @brief Enum overload for tag lookup.
         * @tparam EnumTag Any enum type with integral underlying values.
         */
        template<typename EnumTag>
        DecodedObjectLookup operator[](EnumTag tag) const
            requires(std::is_enum_v<EnumTag>) {
            return (*this)[static_cast<std::uint32_t>(std::to_underlying(tag))];
        }

        /**
         * @brief Indicates whether this lookup resolves to an existing node.
         */
        bool exists() const {
            return node_ != nullptr;
        }

        /**
         * @brief Returns the node value, or `std::monostate` if missing.
         */
        const Value &value() const;

        /**
         * @brief Typed accessor helper based on `std::get_if`.
         * @tparam T Requested variant alternative type.
         * @return Pointer to value if present, otherwise `nullptr`.
         */
        template<typename T>
        const T *as() const {
            return std::get_if<T>(&value());
        }

    private:
        friend struct DecodedObject;

        DecodedObjectLookup(const std::unordered_map<std::uint32_t, DecodedObjectNode> *root,
                            const DecodedObjectNode *node)
            : root_(root)
              , node_(node) {
        }

        const std::unordered_map<std::uint32_t, DecodedObjectNode> *root_ = nullptr;
        const DecodedObjectNode *node_ = nullptr;
    };

    /**
     * @brief Decoded FIX message optimized for enum/tag based object access.
     */
    struct DecodedObject {
        /** Value of tag 8 (BeginString), if present. */
        std::string begin_string;
        /** Value of tag 35 (MsgType), if present. */
        std::string msg_type;
        /** Normalized message storage backing all string_view values. */
        std::string normalized_message;
        /** Root field map indexed by numeric FIX tag. */
        std::unordered_map<std::uint32_t, DecodedObjectNode> fields;
        /** Structural validation status derived from dictionary members/components/groups. */
        bool structurally_valid = true;
        /** Human-readable validation errors when `structurally_valid` is `false`. */
        std::vector<std::string> validation_errors;

        /**
         * @brief Lookup by numeric FIX tag.
         */
        DecodedObjectLookup operator[](std::uint32_t tag) const;

        /**
         * @brief Lookup by generated enum tag (for example `fix::generated::fix42::FieldTag`).
         */
        template<typename EnumTag>
        DecodedObjectLookup operator[](EnumTag tag) const
            requires(std::is_enum_v<EnumTag>) {
            return (*this)[static_cast<std::uint32_t>(std::to_underlying(tag))];
        }
    };

    /**
     * @brief Transparent hasher for string-like keys in unordered maps.
     *
     * Enables heterogeneous lookup using `std::string_view` to avoid temporary
     * `std::string` allocations when searching by `const char*` or string literals.
     */
    struct StringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(const char *txt) const {
            return std::hash<std::string_view>{}(txt);
        }

        [[nodiscard]] std::size_t operator()(std::string_view txt) const {
            return std::hash<std::string_view>{}(txt);
        }

        [[nodiscard]] std::size_t operator()(const std::string &txt) const {
            return std::hash<std::string>{}(txt);
        }
    };

    /**
     * @brief Decodes raw FIX messages using QuickFIX XML dictionaries.
     */
    class Decoder {
    public:
        using ValueIterator = std::string_view::const_iterator;
        using DecodedValue = std::variant<std::monostate, bool, std::int64_t, float, double, std::string_view>;
        using ValueDecoder = std::function<DecodedValue(ValueIterator, ValueIterator)>;

        Decoder();

        /**
         * @brief Loads all dictionary XML files from a directory.
         * @param path Directory containing `.xml` dictionary files.
         * @param error Optional output parameter for a human-readable error message.
         * @return `true` if at least one dictionary was loaded, `false` otherwise.
         */
        bool loadDictionariesFromDirectory(const std::string &path, std::string *error = nullptr) {
            return dictionaries_.loadFromDirectory(path, error);
        }

        /**
         * @brief Decodes a raw FIX message into structured fields.
         * @param raw Raw FIX message using SOH (`0x01`) or `|` as separators.
         * @return Parsed message with known field names resolved from dictionaries.
         */
        DecodedMessage decode(const std::string &raw) const;

        /**
         * @brief Decodes a raw FIX message into an enum-indexable object.
         * @param raw Raw FIX message using SOH (`0x01`) or `|` as separators.
         * @return Object supporting lookup such as `msg[FieldTag::kMsgType]`.
         */
        DecodedObject decodeObject(const std::string &raw) const;

        /**
         * @brief Registers or overrides a value decoder for a FIX dictionary type name.
         * @param type_name Dictionary field type (for example `INT`, `PRICE`, `BOOLEAN`).
         * @param decoder Decoder function that receives `[begin, end)` iterators into message storage.
         */
        void registerTypeDecoder(std::string type_name, ValueDecoder decoder);

    private:
        struct ParsedField {
            std::uint32_t tag = 0;
            std::size_t value_begin = 0;
            std::size_t value_end = 0;
        };

        DictionarySet dictionaries_;
        std::unordered_map<std::string, ValueDecoder, StringHash, std::equal_to<>> value_decoders_;
        std::unordered_map<std::uint8_t, ValueDecoder> decoder_tag_decoders_;

        static std::string normalizeMessage(const std::string &raw);

        static std::vector<ParsedField> splitTags(std::string_view message);

        const Dictionary *selectDictionary(std::string_view message, const std::vector<ParsedField> &fields) const;

        DecodedValue decodeTypedValue(std::uint8_t decoder_tag, ValueIterator begin, ValueIterator end) const;

        DecodedValue decodeTypedValue(const std::string &type, ValueIterator begin, ValueIterator end) const;
    };
} // namespace fix

#endif  // FIXDECODER_FIX_DECODER_H_INCLUDED
