/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/examples.cc
 * Description: CLI examples for decoding FIX messages and generating objects.
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
#include "fix_msgtype_key.h"

#include <FIX42_decoder_map.h>
#include <generator_map.h>
#include <msg_generated_object.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace
{

struct NewOrderSingle final : util::MsgGeneratedObjectIfc
{
    std::string cl_ord_id;
    std::string symbol;
};

std::string findFieldValue(const fix::DecodedMessage &decoded, std::uint32_t tag)
{
    for(const auto &field : decoded.fields)
    {
        if(field.tag == tag)
        {
            return std::string(field.value);
        }
    }
    return {};
}

void printDecodedMessage(const fix::DecodedMessage &decoded)
{
    std::cout << "BeginString: " << decoded.begin_string << "\n";
    std::cout << "MsgType: " << decoded.msg_type << "\n";
    std::cout << "Fields:\n";

    for(const auto &field : decoded.fields)
    {
        std::cout << "  " << field.tag;
        if(!field.name.empty())
        {
            std::cout << " (" << field.name << ")";
        }
        std::cout << " = " << field.value;

        if(const auto as_bool = std::get_if<bool>(&field.typed_value))
        {
            std::cout << " [typed bool: " << (*as_bool ? "true" : "false") << "]";
        }
        else if(const auto as_int = std::get_if<std::int64_t>(&field.typed_value))
        {
            std::cout << " [typed int: " << *as_int << "]";
        }
        else if(const auto as_float = std::get_if<float>(&field.typed_value))
        {
            std::cout << " [typed float: " << *as_float << "]";
        }
        else if(const auto as_double = std::get_if<double>(&field.typed_value))
        {
            std::cout << " [typed double: " << *as_double << "]";
        }
        else if(const auto as_string = std::get_if<std::string_view>(&field.typed_value))
        {
            std::cout << " [typed string: " << *as_string << "]";
        }

        std::cout << "\n";
    }
}

void runBasicDecodeExample(const fix::Decoder &decoder, const std::string &message)
{
    std::cout << "\n=== Example 1: Basic decode() output ===\n";
    const fix::DecodedMessage decoded = decoder.decode(message);
    printDecodedMessage(decoded);
}

void runDecodeObjectExample(const fix::Decoder &decoder, const std::string &message)
{
    std::cout << "\n=== Example 2: decodeObject() enum-based access ===\n";
    const fix::DecodedObject decoded = decoder.decodeObject(message);

    const auto symbol = decoded[fix::generated::fix42::FieldTag::kSymbol].as<std::string_view>();
    const auto quantity = decoded[fix::generated::fix42::FieldTag::kOrderQty].as<double>();
    const auto price = decoded[fix::generated::fix42::FieldTag::kPrice].as<double>();

    if(symbol && quantity && price)
    {
        std::cout << "Symbol=" << *symbol << " OrderQty=" << *quantity << " Price=" << *price << "\n";
    }
    else
    {
        std::cout << "Expected FIX.4.2 symbol/qty/price fields are missing.\n";
    }

    const auto fallback_symbol = decoded[fix::generated::fix42::FieldTag::kMsgType]
                                      [fix::generated::fix42::FieldTag::kSymbol]
                                          .as<std::string_view>();
    if(fallback_symbol)
    {
        std::cout << "Chained lookup fallback symbol=" << *fallback_symbol << "\n";
    }
}

void runApplVerIdSelectionExample(const fix::Decoder &decoder, const std::string &message)
{
    std::cout << "\n=== Example 3: FIXT.1.1 transport + ApplVerID routing ===\n";
    const fix::DecodedMessage decoded = decoder.decode(message);

    std::cout << "BeginString: " << decoded.begin_string << " MsgType: " << decoded.msg_type << "\n";

    for(const auto &field : decoded.fields)
    {
        if(field.tag == 44)
        {
            std::cout << "Tag 44 resolved as ";
            if(!field.name.empty())
            {
                std::cout << field.name;
            }
            else
            {
                std::cout << "<unknown>";
            }
            std::cout << " with typed value ";
            if(const auto price = std::get_if<double>(&field.typed_value))
            {
                std::cout << *price << "\n";
            }
            else
            {
                std::cout << "<not double>\n";
            }
            return;
        }
    }

    std::cout << "Tag 44 was not found in the message.\n";
}

void runGeneratedObjectExample(fix::Decoder &decoder, const std::string &message)
{
    std::cout << "\n=== Example 4: generator_map object creation by MsgType ===\n";

    using Map = util::generator_map<8, fix::fix_msg_key>;

    Map::registerGenerator("35=D|", [&](const std::string &raw) {
        const fix::DecodedMessage decoded = decoder.decode(raw);
        auto obj = std::make_shared<NewOrderSingle>();
        obj->cl_ord_id = findFieldValue(decoded, 11);
        obj->symbol = findFieldValue(decoded, 55);
        return obj;
    });

    auto generated = Map::get(message);
    if(!generated)
    {
        std::cout << "No generator matched message type.\n";
        return;
    }

    auto order = std::static_pointer_cast<NewOrderSingle>(generated);
    std::cout << "Generated NewOrderSingle: ClOrdID=" << order->cl_ord_id
              << " Symbol=" << order->symbol << "\n";
}

}  // namespace

int main(int argc, char **argv)
{
    const std::string dictionary_directory = (argc > 1) ? argv[1] : "data/quickfix";

    const std::string basic_decode_message =
        (argc > 2)
            ? argv[2]
            : "8=FIX.4.2|9=65|35=D|49=BUY|56=SELL|34=2|52=20100225-19:41:57.316|11=ABC|21=1|55=IBM|54=1|60=20100225-19:41:57.316|38=100|40=1|10=062|";

    const std::string object_decode_message =
        (argc > 3)
            ? argv[3]
            : "8=FIX.4.2|9=61|35=T|55=IBM|38=100|44=123.45|10=000|";

    const std::string appl_ver_id_message =
        (argc > 4)
            ? argv[4]
            : "8=FIXT.1.1|9=108|35=D|1128=9|49=BUY|56=SELL|34=2|52=20260211-12:00:00.000|11=DEF|55=MSFT|54=1|60=20260211-12:00:00.000|38=250|40=2|44=420.50|10=000|";

    fix::Decoder decoder;
    std::string error;

    if(!decoder.loadDictionariesFromDirectory(dictionary_directory, &error))
    {
        std::cerr << "Dictionary load warning: " << error << "\n";
    }

    std::cout << "Dictionary directory: " << dictionary_directory << "\n";

    runBasicDecodeExample(decoder, basic_decode_message);
    runDecodeObjectExample(decoder, object_decode_message);
    runApplVerIdSelectionExample(decoder, appl_ver_id_message);
    runGeneratedObjectExample(decoder, basic_decode_message);

    return 0;
}
