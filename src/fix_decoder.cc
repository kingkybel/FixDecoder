/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/fix_decoder.cc
 * Description: Decoder implementation for FIX messages.
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

#include "fix_decoder.h"

#include "FIX40_decoder_map.h"
#include "FIX41_decoder_map.h"
#include "FIX42_decoder_map.h"
#include "FIX43_decoder_map.h"
#include "FIX44_decoder_map.h"
#include "FIX50SP1_decoder_map.h"
#include "FIX50SP2_decoder_map.h"
#include "FIX50_decoder_map.h"
#include "FIXT11_decoder_map.h"
#include "decoder_tag.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fix
{

namespace
{

    std::string applicationVersionIdToBeginString(const std::string &value)
    {
        if(value == "2")
        {
            return "FIX.4.0";
        }
        if(value == "3")
        {
            return "FIX.4.1";
        }
        if(value == "4")
        {
            return "FIX.4.2";
        }
        if(value == "5")
        {
            return "FIX.4.3";
        }
        if(value == "6")
        {
            return "FIX.4.4";
        }
        if(value == "7" || value == "8" || value == "9")
        {
            return "FIX.5.0";
        }

        return value;
    }

    std::string toUpperCopy(std::string value)
    {
        std::ranges::transform(value,
                               value.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    }

    std::string_view toView(const Decoder::ValueIterator begin, const Decoder::ValueIterator end)
    {
        if(begin == end)
        {
            return std::string_view{};
        }
        return std::string_view(&*begin, static_cast<std::size_t>(end - begin));
    }

    using GeneratedDecoderTag = generated::DecoderTag;
    using DecoderTagResolver  = GeneratedDecoderTag (*)(std::uint32_t);

    struct VersionDecoderSelection
    {
        std::string        begin_string;
        DecoderTagResolver resolver = nullptr;
    };

    std::string extractTagValue(const std::string_view message, const int wanted_tag)
    {
        static constexpr char soh   = 0x01;
        std::size_t           start = 0;
        while(start < message.size())
        {
            const std::size_t end       = message.find(soh, start);
            const std::size_t token_end = (end == std::string_view::npos) ? message.size() : end;
            const std::size_t eq_pos    = message.find('=', start);

            if(eq_pos != std::string_view::npos && eq_pos < token_end)
            {
                int parsed_tag       = 0;
                const auto [ptr, ec] = std::from_chars(message.data() + start, message.data() + eq_pos, parsed_tag);
                if(ec == std::errc{} && ptr == message.data() + eq_pos && parsed_tag == wanted_tag)
                {
                    const std::size_t value_len = token_end - (eq_pos + 1);
                    return std::string(message.substr(eq_pos + 1, value_len));
                }
            }

            if(end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
        }
        return {};
    }

    VersionDecoderSelection selectVersionDecoder(const std::string_view message)
    {
        std::string       begin_string = extractTagValue(message, 8);
        const std::string appl_ver_id  = extractTagValue(message, 1128);

        std::string effective_begin = std::move(begin_string);
        if(!appl_ver_id.empty())
        {
            effective_begin = applicationVersionIdToBeginString(appl_ver_id);
        }

        DecoderTagResolver resolver = nullptr;
        if(effective_begin == generated::fix40::kBeginString)
        {
            resolver = generated::fix40::decoderTagFor;
        }
        else if(effective_begin == generated::fix41::kBeginString)
        {
            resolver = generated::fix41::decoderTagFor;
        }
        else if(effective_begin == generated::fix42::kBeginString)
        {
            resolver = generated::fix42::decoderTagFor;
        }
        else if(effective_begin == generated::fix43::kBeginString)
        {
            resolver = generated::fix43::decoderTagFor;
        }
        else if(effective_begin == generated::fix44::kBeginString)
        {
            resolver = generated::fix44::decoderTagFor;
        }
        else if(effective_begin == generated::fix50::kBeginString)
        {
            resolver = generated::fix50::decoderTagFor;
        }
        else if(effective_begin == generated::fixt11::kBeginString)
        {
            resolver = generated::fixt11::decoderTagFor;
        }

        return VersionDecoderSelection{std::move(effective_begin), resolver};
    }

    std::uint8_t toDecoderTagValue(const GeneratedDecoderTag tag)
    {
        return static_cast<std::uint8_t>(tag);
    }

    const DecodedObjectNode::Value &missingLookupValue()
    {
        static const DecodedObjectNode::Value missing_value{};
        return missing_value;
    }

    struct ValidationField
    {
        std::uint32_t tag        = 0;
        std::size_t   value_begin = 0;
        std::size_t   value_end   = 0;
    };

    using ValidationErrors = std::vector<std::string>;

    std::optional<std::uint32_t> firstMemberTag(const Dictionary &dict, const std::vector<Member> &members);

    std::optional<std::uint32_t> firstMemberTag(const Dictionary &dict, const Member &member)
    {
        if(member.kind == MemberKind::Field || member.kind == MemberKind::Group)
        {
            if(const FieldDef *def = dict.fieldByName(member.name))
            {
                return def->number;
            }
            return std::nullopt;
        }

        const std::vector<Member> *component_members = dict.componentByName(member.name);
        if(!component_members)
        {
            return std::nullopt;
        }
        return firstMemberTag(dict, *component_members);
    }

    std::optional<std::uint32_t> firstMemberTag(const Dictionary &dict, const std::vector<Member> &members)
    {
        for(const Member &member: members)
        {
            if(const std::optional<std::uint32_t> tag = firstMemberTag(dict, member))
            {
                return tag;
            }
        }
        return std::nullopt;
    }

    bool parseMembersForValidation(const Dictionary                 &dict,
                                   const std::vector<Member>       &members,
                                   const std::string_view           message,
                                   const std::vector<ValidationField> &fields,
                                   std::size_t                     &index,
                                   ValidationErrors                &errors,
                                   bool                             enforce_presence);

    bool parseMemberForValidation(const Dictionary                  &dict,
                                  const Member                      &member,
                                  const std::string_view             message,
                                  const std::vector<ValidationField> &fields,
                                  std::size_t                       &index,
                                  ValidationErrors                  &errors,
                                  const bool                         enforce_presence)
    {
        if(member.kind == MemberKind::Field)
        {
            const FieldDef *def = dict.fieldByName(member.name);
            if(!def)
            {
                return false;
            }

            if(index < fields.size() && fields[index].tag == def->number)
            {
                ++index;
                return true;
            }

            if(member.required && enforce_presence)
            {
                errors.emplace_back("Missing required field '" + member.name + "'");
            }
            return false;
        }

        if(member.kind == MemberKind::Component)
        {
            const std::vector<Member> *component_members = dict.componentByName(member.name);
            if(!component_members)
            {
                if(member.required && enforce_presence)
                {
                    errors.emplace_back("Missing required component '" + member.name + "'");
                }
                return false;
            }

            const std::optional<std::uint32_t> expected_tag = firstMemberTag(dict, *component_members);
            if(expected_tag && (index >= fields.size() || fields[index].tag != *expected_tag))
            {
                if(member.required && enforce_presence)
                {
                    errors.emplace_back("Missing required component '" + member.name + "'");
                }
                return false;
            }

            const std::size_t start_index = index;
            parseMembersForValidation(dict, *component_members, message, fields, index, errors, true);
            const bool consumed = index > start_index;

            if(member.required && enforce_presence && !consumed)
            {
                errors.emplace_back("Missing required component '" + member.name + "'");
            }
            return consumed;
        }

        const FieldDef *count_def = dict.fieldByName(member.name);
        if(!count_def)
        {
            return false;
        }

        if(index >= fields.size() || fields[index].tag != count_def->number)
        {
            if(member.required && enforce_presence)
            {
                errors.emplace_back("Missing required group-count field '" + member.name + "'");
            }
            return false;
        }

        const ValidationField &count_field = fields[index];
        const std::string_view count_value =
         message.substr(count_field.value_begin, count_field.value_end - count_field.value_begin);
        int declared_count   = 0;
        const auto [ptr, ec] = std::from_chars(count_value.data(), count_value.data() + count_value.size(), declared_count);
        if(ec != std::errc{} || ptr != count_value.data() + count_value.size() || declared_count < 0)
        {
            errors.emplace_back("Invalid group-count value for '" + member.name + "'");
            ++index;
            return true;
        }

        ++index;
        std::size_t actual_count = 0;
        for(int i = 0; i < declared_count; ++i)
        {
            const std::size_t entry_start = index;
            parseMembersForValidation(dict, member.children, message, fields, index, errors, true);
            if(index == entry_start)
            {
                break;
            }
            ++actual_count;
        }

        if(actual_count != static_cast<std::size_t>(declared_count))
        {
            errors.emplace_back("Group '" + member.name + "' count mismatch: declared "
                                + std::to_string(declared_count) + ", actual " + std::to_string(actual_count));
        }

        return true;
    }

    bool parseMembersForValidation(const Dictionary                 &dict,
                                   const std::vector<Member>       &members,
                                   const std::string_view           message,
                                   const std::vector<ValidationField> &fields,
                                   std::size_t                     &index,
                                   ValidationErrors                &errors,
                                   const bool                       enforce_presence)
    {
        bool consumed_any = false;
        for(const Member &member: members)
        {
            const std::size_t before = index;
            parseMemberForValidation(dict, member, message, fields, index, errors, enforce_presence);
            if(index > before)
            {
                consumed_any = true;
            }
        }
        return consumed_any;
    }

    ValidationErrors validateStructure(const Dictionary                   &dict,
                                       const std::string                  &msg_type,
                                       const std::string_view              message,
                                       const std::vector<ValidationField> &fields)
    {
        ValidationErrors errors;
        if(msg_type.empty())
        {
            return errors;
        }

        const MessageDef *message_def = dict.messageByType(msg_type);
        if(!message_def)
        {
            return errors;
        }

        std::size_t index = 0;
        bool        positioned = false;
        if(const std::optional<std::uint32_t> start_tag = firstMemberTag(dict, message_def->members))
        {
            while(index < fields.size())
            {
                if(fields[index].tag == *start_tag)
                {
                    positioned = true;
                    break;
                }
                ++index;
            }
        }

        if(!positioned)
        {
            index = 0;
            while(index < fields.size())
            {
                bool matches_member = false;
                for(const Member &member: message_def->members)
                {
                    const std::optional<std::uint32_t> member_tag = firstMemberTag(dict, member);
                    if(member_tag && fields[index].tag == *member_tag)
                    {
                        matches_member = true;
                        break;
                    }
                }
                if(matches_member)
                {
                    break;
                }
                ++index;
            }
        }

        // Enforce required members for message-level semantic validation.
        parseMembersForValidation(dict, message_def->members, message, fields, index, errors, true);
        return errors;
    }

}  // namespace

DecodedObjectLookup DecodedObject::operator[](const std::uint32_t tag) const
{
    const auto it = fields.find(tag);
    if(it == fields.end())
    {
        return DecodedObjectLookup(&fields, nullptr);
    }
    return DecodedObjectLookup(&fields, &it->second);
}

DecodedObjectLookup DecodedObjectLookup::operator[](const std::uint32_t tag) const
{
    if(node_)
    {
        const auto child_it = node_->children.find(tag);
        if(child_it != node_->children.end())
        {
            return DecodedObjectLookup(root_, &child_it->second);
        }
    }

    if(!root_)
    {
        return DecodedObjectLookup(nullptr, nullptr);
    }

    const auto root_it = root_->find(tag);
    if(root_it == root_->end())
    {
        return DecodedObjectLookup(root_, nullptr);
    }
    return DecodedObjectLookup(root_, &root_it->second);
}

const DecodedObjectLookup::Value &DecodedObjectLookup::value() const
{
    if(!node_)
    {
        return missingLookupValue();
    }
    return node_->value;
}

Decoder::Decoder()
{
    registerTypeDecoder("BOOLEAN",
                        [](ValueIterator begin, ValueIterator end) -> DecodedValue
                        {
                            const std::string_view value = toView(begin, end);
                            if(value == "Y" || value == "y" || value == "1" || value == "TRUE" || value == "true")
                            {
                                return true;
                            }
                            if(value == "N" || value == "n" || value == "0" || value == "FALSE" || value == "false")
                            {
                                return false;
                            }
                            return std::monostate{};
                        });

    registerTypeDecoder("INT",
                        [](ValueIterator begin, ValueIterator end) -> DecodedValue
                        {
                            const std::string_view value  = toView(begin, end);
                            std::int64_t           parsed = 0;
                            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                            if(ec == std::errc{} && ptr == value.data() + value.size())
                            {
                                return parsed;
                            }
                            return std::monostate{};
                        });

    registerTypeDecoder("NUMINGROUP", value_decoders_["INT"]);
    registerTypeDecoder("SEQNUM", value_decoders_["INT"]);
    registerTypeDecoder("LENGTH", value_decoders_["INT"]);

    registerTypeDecoder("FLOAT",
                        [](ValueIterator begin, ValueIterator end) -> DecodedValue
                        {
                            const std::string_view value  = toView(begin, end);
                            float                  parsed = 0.0F;
                            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                            if(ec == std::errc{} && ptr == value.data() + value.size())
                            {
                                return parsed;
                            }
                            return std::monostate{};
                        });

    registerTypeDecoder("DOUBLE",
                        [](ValueIterator begin, ValueIterator end) -> DecodedValue
                        {
                            const std::string_view value  = toView(begin, end);
                            double                 parsed = 0.0;
                            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                            if(ec == std::errc{} && ptr == value.data() + value.size())
                            {
                                return parsed;
                            }
                            return std::monostate{};
                        });

    registerTypeDecoder("AMT", value_decoders_["DOUBLE"]);
    registerTypeDecoder("PRICE", value_decoders_["DOUBLE"]);
    registerTypeDecoder("PRICEOFFSET", value_decoders_["DOUBLE"]);
    registerTypeDecoder("PERCENTAGE", value_decoders_["DOUBLE"]);
    registerTypeDecoder("QTY", value_decoders_["DOUBLE"]);

    registerTypeDecoder("STRING",
                        [](ValueIterator begin, ValueIterator end) -> DecodedValue { return toView(begin, end); });

    registerTypeDecoder("CHAR", value_decoders_["STRING"]);
    registerTypeDecoder("MULTIPLECHARVALUE", value_decoders_["STRING"]);
    registerTypeDecoder("MULTIPLESTRINGVALUE", value_decoders_["STRING"]);
    registerTypeDecoder("EXCHANGE", value_decoders_["STRING"]);
    registerTypeDecoder("CURRENCY", value_decoders_["STRING"]);
    registerTypeDecoder("UTCTIMESTAMP", value_decoders_["STRING"]);
    registerTypeDecoder("UTCTIMEONLY", value_decoders_["STRING"]);
    registerTypeDecoder("UTCDATEONLY", value_decoders_["STRING"]);
    registerTypeDecoder("LOCALMKTDATE", value_decoders_["STRING"]);
    registerTypeDecoder("MONTHYEAR", value_decoders_["STRING"]);
    registerTypeDecoder("DAYOFMONTH", value_decoders_["STRING"]);
    registerTypeDecoder("DATA", value_decoders_["STRING"]);
    registerTypeDecoder("COUNTRY", value_decoders_["STRING"]);
    registerTypeDecoder("LANGUAGE", value_decoders_["STRING"]);

    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kBool)]       = value_decoders_["BOOLEAN"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kInt64)]      = value_decoders_["INT"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kFloat)]      = value_decoders_["FLOAT"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kDouble)]     = value_decoders_["DOUBLE"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kString)]     = value_decoders_["STRING"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kGroupCount)] = value_decoders_["INT"];
    decoder_tag_decoders_[toDecoderTagValue(GeneratedDecoderTag::kRawData)]    = value_decoders_["STRING"];
}

void Decoder::registerTypeDecoder(std::string type_name, ValueDecoder decoder)
{
    std::string normalized = toUpperCopy(std::move(type_name));
    value_decoders_.insert_or_assign(std::move(normalized), std::move(decoder));
}

std::string Decoder::normalizeMessage(const std::string &raw)
{
    static constexpr char soh  = 0x01;
    static constexpr char pipe = 0x7c;

    if(raw.find(soh) == std::string::npos && raw.find(pipe) != std::string::npos)
    {
        std::string normalized = raw;
        std::replace(normalized.begin(), normalized.end(), pipe, soh);
        return normalized;
    }
    return raw;
}

std::vector<Decoder::ParsedField> Decoder::splitTags(const std::string_view message)
{
    static constexpr char soh = 0x01;

    std::vector<ParsedField> result;

    std::size_t start = 0;
    while(start < message.size())
    {
        const std::size_t end       = message.find(soh, start);
        const std::size_t token_end = (end == std::string_view::npos) ? message.size() : end;
        const std::size_t eq_pos    = message.find('=', start);

        if(eq_pos != std::string_view::npos && eq_pos < token_end)
        {
            int tag              = 0;
            const auto [ptr, ec] = std::from_chars(message.data() + start, message.data() + eq_pos, tag);
            if(ec == std::errc{} && ptr == message.data() + eq_pos && tag > 0)
            {
                result.emplace_back(ParsedField{static_cast<std::uint32_t>(tag), eq_pos + 1, token_end});
            }
        }

        if(end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }

    return result;
}

const Dictionary *Decoder::selectDictionary(const std::string_view          message,
                                            const std::vector<ParsedField> &fields) const
{
    std::string begin_string;
    std::string appl_ver_id;

    for(const auto &field: fields)
    {
        const std::string_view value(message.data() + field.value_begin, field.value_end - field.value_begin);
        if(field.tag == 8)
        {
            begin_string.assign(value.begin(), value.end());
        }
        else if(field.tag == 1128 && appl_ver_id.empty())
        {
            appl_ver_id.assign(value.begin(), value.end());
        }
    }

    if(!appl_ver_id.empty())
    {
        const std::string mapped = applicationVersionIdToBeginString(appl_ver_id);
        if(const Dictionary *dict = dictionaries_.findByBeginString(mapped))
        {
            return dict;
        }
    }

    if(!begin_string.empty())
    {
        return dictionaries_.findByBeginString(begin_string);
    }

    return nullptr;
}

Decoder::DecodedValue
 Decoder::decodeTypedValue(const std::uint8_t decoder_tag, const ValueIterator begin, const ValueIterator end) const
{
    const auto it = decoder_tag_decoders_.find(decoder_tag);
    if(it != decoder_tag_decoders_.end())
    {
        return it->second(begin, end);
    }

    const auto string_it = decoder_tag_decoders_.find(toDecoderTagValue(GeneratedDecoderTag::kString));
    if(string_it != decoder_tag_decoders_.end())
    {
        return string_it->second(begin, end);
    }

    return std::monostate{};
}

Decoder::DecodedValue
 Decoder::decodeTypedValue(const std::string &type, const ValueIterator begin, const ValueIterator end) const
{
    const std::string key = toUpperCopy(type);
    const auto        it  = value_decoders_.find(key);
    if(it != value_decoders_.end())
    {
        return it->second(begin, end);
    }

    const auto string_it = value_decoders_.find("STRING");
    if(string_it != value_decoders_.end())
    {
        return string_it->second(begin, end);
    }

    return std::monostate{};
}

DecodedMessage Decoder::decode(const std::string &raw) const
{
    DecodedMessage decoded;

    decoded.normalized_message = normalizeMessage(raw);
    const std::string_view message(decoded.normalized_message);
    const auto             fields          = splitTags(message);
    const auto             version_decoder = selectVersionDecoder(message);
    const Dictionary      *dict            = selectDictionary(message, fields);

    decoded.fields.reserve(fields.size());

    std::vector<ValidationField> validation_fields;
    validation_fields.reserve(fields.size());

    for(const auto &parsed: fields)
    {
        validation_fields.push_back(ValidationField{parsed.tag, parsed.value_begin, parsed.value_end});

        DecodedField field;
        field.tag   = parsed.tag;
        field.value = message.substr(parsed.value_begin, parsed.value_end - parsed.value_begin);

        if(parsed.tag == 8)
        {
            decoded.begin_string.assign(field.value.begin(), field.value.end());
        }
        if(parsed.tag == 35)
        {
            decoded.msg_type.assign(field.value.begin(), field.value.end());
        }

        if(dict)
        {
            if(const FieldDef *def = dict->fieldByNumber(parsed.tag))
            {
                field.name = def->name;
                field.type = def->type;
            }
        }

        const auto begin = message.begin() + static_cast<std::ptrdiff_t>(parsed.value_begin);
        const auto end   = message.begin() + static_cast<std::ptrdiff_t>(parsed.value_end);
        if(version_decoder.resolver)
        {
            const auto decoder_tag = version_decoder.resolver(parsed.tag);
            field.typed_value      = decodeTypedValue(toDecoderTagValue(decoder_tag), begin, end);
        }
        else
        {
            field.typed_value = decodeTypedValue(field.type, begin, end);
        }

        decoded.fields.push_back(std::move(field));
    }

    if(dict)
    {
        decoded.validation_errors = validateStructure(*dict, decoded.msg_type, message, validation_fields);
        decoded.structurally_valid = decoded.validation_errors.empty();
    }

    return decoded;
}

DecodedObject Decoder::decodeObject(const std::string &raw) const
{
    DecodedObject decoded;

    decoded.normalized_message = normalizeMessage(raw);
    const std::string_view message(decoded.normalized_message);
    const auto             fields          = splitTags(message);
    auto                   version_decoder = selectVersionDecoder(message);
    const Dictionary      *dict            = selectDictionary(message, fields);

    std::vector<ValidationField> validation_fields;
    validation_fields.reserve(fields.size());

    if(!version_decoder.begin_string.empty())
    {
        decoded.begin_string = std::move(version_decoder.begin_string);
    }

    for(const auto &parsed: fields)
    {
        validation_fields.push_back(ValidationField{parsed.tag, parsed.value_begin, parsed.value_end});

        const std::string_view value = message.substr(parsed.value_begin, parsed.value_end - parsed.value_begin);
        if(parsed.tag == 8 && decoded.begin_string.empty())
        {
            decoded.begin_string.assign(value.begin(), value.end());
        }
        if(parsed.tag == 35 && decoded.msg_type.empty())
        {
            decoded.msg_type.assign(value.begin(), value.end());
        }

        const auto begin = message.begin() + static_cast<std::ptrdiff_t>(parsed.value_begin);
        const auto end   = message.begin() + static_cast<std::ptrdiff_t>(parsed.value_end);

        DecodedValue typed_value = std::monostate{};
        if(version_decoder.resolver)
        {
            const auto decoder_tag = version_decoder.resolver(parsed.tag);
            typed_value            = decodeTypedValue(toDecoderTagValue(decoder_tag), begin, end);
        }
        else
        {
            typed_value = decodeTypedValue("STRING", begin, end);
        }

        auto [it, inserted] = decoded.fields.try_emplace(parsed.tag);
        if(inserted)
        {
            it->second.value = std::move(typed_value);
        }
    }

    if(dict)
    {
        decoded.validation_errors = validateStructure(*dict, decoded.msg_type, message, validation_fields);
        decoded.structurally_valid = decoded.validation_errors.empty();
    }

    return decoded;
}

}  // namespace fix
