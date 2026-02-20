/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   test/fixdecoder_tests.cc
 * Description: GoogleTest suite for FixDecoder.
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

#include "FIX42_decoder_map.h"
#include "fix_decoder.h"
#include "fix_dictionary.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

class TempDir
{
    public:
    TempDir()
    {
        const auto        base = std::filesystem::temp_directory_path();
        const std::string name = "fixdecoder_test_" + std::to_string(::getpid()) + "_"
                                 + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        path_ = base / name;
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path &path() const
    {
        return path_;
    }

    private:
    std::filesystem::path path_;
};

bool writeFile(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream out(path);
    if(!out)
    {
        return false;
    }
    out << contents;
    return out.good();
}

std::vector<std::string> readMessageFile(const std::filesystem::path &path)
{
    std::ifstream            in(path);
    std::vector<std::string> messages;

    std::string line;
    while(std::getline(in, line))
    {
        if(line.empty() || line[0] == '#')
        {
            continue;
        }
        messages.emplace_back(std::move(line));
    }

    return messages;
}

bool hasNamedTag(const fix::DecodedMessage &decoded, std::uint32_t tag)
{
    for(const auto &field: decoded.fields)
    {
        if(field.tag == tag)
        {
            return !field.name.empty();
        }
    }
    return false;
}

bool looksValidDecode(const fix::DecodedMessage &decoded)
{
    if(decoded.begin_string.empty() || decoded.msg_type.empty())
    {
        return false;
    }
    if(decoded.fields.size() < 3)
    {
        return false;
    }
    return hasNamedTag(decoded, 8) && hasNamedTag(decoded, 35);
}

std::string removeTag8(const std::string &message)
{
    const std::string needle = "8=";
    const std::size_t start  = message.find(needle);
    if(start == std::string::npos)
    {
        return message;
    }

    const std::size_t end = message.find('|', start);
    if(end == std::string::npos)
    {
        return message.substr(0, start);
    }

    std::string mutated = message;
    mutated.erase(start, end - start + 1);
    return mutated;
}

std::string breakMsgTypeTag(const std::string &message)
{
    std::string       mutated = message;
    const std::size_t pos     = mutated.find("|35=");
    if(pos != std::string::npos)
    {
        mutated.replace(pos + 1, 3, "35-");
    }
    return mutated;
}

std::string makeBeginTagNonNumeric(const std::string &message)
{
    std::string       mutated = message;
    const std::size_t pos     = mutated.find("8=");
    if(pos != std::string::npos)
    {
        mutated[pos] = 'X';
    }
    return mutated;
}

const char *kMinimalFix42 = "<?xml version=\"1.0\"?>\n"
                            "<fix type=\"FIX\" major=\"4\" minor=\"2\">\n"
                            "  <fields>\n"
                            "    <field number=\"8\" name=\"BeginString\" type=\"STRING\"/>\n"
                            "    <field number=\"35\" name=\"MsgType\" type=\"STRING\"/>\n"
                            "    <field number=\"55\" name=\"Symbol\" type=\"STRING\"/>\n"
                            "  </fields>\n"
                            "  <messages>\n"
                            "    <message name=\"TestMsg\" msgtype=\"T\" msgcat=\"app\">\n"
                            "      <field name=\"Symbol\" required=\"Y\"/>\n"
                            "    </message>\n"
                            "  </messages>\n"
                            "</fix>\n";

struct SampleSet
{
    std::string file_name;
    std::string begin_string;
};

class SampleMessagesTest : public ::testing::TestWithParam<SampleSet>
{
};

class RealisticSubsetMessagesTest : public ::testing::TestWithParam<SampleSet>
{
};

}  // namespace

TEST(DictionaryTest, LoadsFileAndResolvesFieldAndMessage)
{
    TempDir    temp;
    const auto xml_path = temp.path() / "FIX42.xml";
    ASSERT_TRUE(writeFile(xml_path, kMinimalFix42));

    fix::Dictionary dict;
    std::string     error;
    ASSERT_TRUE(dict.loadFromFile(xml_path.string(), &error)) << error;

    EXPECT_EQ(dict.beginString(), "FIX.4.2");

    const fix::FieldDef *field = dict.fieldByNumber(55);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, "Symbol");

    const fix::MessageDef *message = dict.messageByType("T");
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->name, "TestMsg");
}

TEST(DictionarySetTest, LoadsDirectoryAndFindsByBeginString)
{
    TempDir    temp;
    const auto xml_path = temp.path() / "FIX42.xml";
    ASSERT_TRUE(writeFile(xml_path, kMinimalFix42));

    fix::DictionarySet set;
    std::string        error;
    ASSERT_TRUE(set.loadFromDirectory(temp.path().string(), &error)) << error;

    const fix::Dictionary *dict = set.findByBeginString("FIX.4.2");
    EXPECT_NE(dict, nullptr);
}

TEST(DecoderTest, AssignsFieldNamesFromDictionary)
{
    TempDir    temp;
    const auto xml_path = temp.path() / "FIX42.xml";
    ASSERT_TRUE(writeFile(xml_path, kMinimalFix42));

    fix::Decoder decoder;
    std::string  error;
    ASSERT_TRUE(decoder.loadDictionariesFromDirectory(temp.path().string(), &error)) << error;

    const std::string         raw     = "8=FIX.4.2|35=T|55=IBM|";
    const fix::DecodedMessage decoded = decoder.decode(raw);

    EXPECT_EQ(decoded.begin_string, "FIX.4.2");

    bool saw_symbol = false;
    for(const auto &field: decoded.fields)
    {
        if(field.tag == 55)
        {
            EXPECT_EQ(field.name, "Symbol");
            saw_symbol = true;
        }
    }

    EXPECT_TRUE(saw_symbol);
}

TEST(DecoderTest, DecodeObjectSupportsEnumLookup)
{
    fix::Decoder decoder;

    const std::string        raw     = "8=FIX.4.2|35=T|55=IBM|38=100|44=123.45|";
    const fix::DecodedObject decoded = decoder.decodeObject(raw);

    EXPECT_EQ(decoded.begin_string, "FIX.4.2");
    EXPECT_EQ(decoded.msg_type, "T");

    const auto symbol = decoded[fix::generated::fix42::FieldTag::kSymbol];
    ASSERT_TRUE(symbol.exists());
    ASSERT_NE(symbol.as<std::string_view>(), nullptr);
    EXPECT_EQ(*symbol.as<std::string_view>(), "IBM");

    const auto order_qty = decoded[fix::generated::fix42::FieldTag::kOrderQty];
    ASSERT_TRUE(order_qty.exists());
    ASSERT_NE(order_qty.as<double>(), nullptr);
    EXPECT_DOUBLE_EQ(*order_qty.as<double>(), 100.0);

    const auto price = decoded[fix::generated::fix42::FieldTag::kPrice];
    ASSERT_TRUE(price.exists());
    ASSERT_NE(price.as<double>(), nullptr);
    EXPECT_DOUBLE_EQ(*price.as<double>(), 123.45);
}

TEST(DecoderTest, DecodeObjectSupportsChainedLookupFallback)
{
    fix::Decoder decoder;

    const std::string        raw     = "8=FIX.4.2|35=T|55=IBM|";
    const fix::DecodedObject decoded = decoder.decodeObject(raw);

    const auto chained_symbol =
     decoded[fix::generated::fix42::FieldTag::kMsgType][fix::generated::fix42::FieldTag::kSymbol];
    ASSERT_TRUE(chained_symbol.exists());
    ASSERT_NE(chained_symbol.as<std::string_view>(), nullptr);
    EXPECT_EQ(*chained_symbol.as<std::string_view>(), "IBM");
}

TEST(DictionaryTest, RequiredAttributeParsing)
{
    EXPECT_TRUE(fix::Dictionary::isRequiredAttr("Y"));
    EXPECT_TRUE(fix::Dictionary::isRequiredAttr("y"));
    EXPECT_FALSE(fix::Dictionary::isRequiredAttr("N"));
    EXPECT_FALSE(fix::Dictionary::isRequiredAttr(nullptr));
}

TEST_P(SampleMessagesTest, ValidSamplesDecode)
{
    const auto sample = GetParam();

    fix::Decoder decoder;
    std::string  error;
    ASSERT_TRUE(
     decoder.loadDictionariesFromDirectory((std::filesystem::path(FIXDECODER_SOURCE_DIR) / "data/quickfix").string(),
                                           &error))
     << error;

    const auto file_path = std::filesystem::path(FIXDECODER_SOURCE_DIR) / "data/samples/valid" / sample.file_name;
    const auto messages  = readMessageFile(file_path);

    ASSERT_FALSE(messages.empty()) << "No sample messages in " << file_path.string();
    EXPECT_GE(messages.size(), 10U) << "Sample set is too small: " << file_path.string();

    for(const auto &message: messages)
    {
        const fix::DecodedMessage decoded = decoder.decode(message);
        EXPECT_TRUE(looksValidDecode(decoded)) << "Expected valid decode for: " << message;
        EXPECT_EQ(decoded.begin_string, sample.begin_string) << "Unexpected BeginString for: " << message;
    }
}

TEST_P(SampleMessagesTest, MutatedSamplesFailDecodeChecks)
{
    const auto sample = GetParam();

    fix::Decoder decoder;
    std::string  error;
    ASSERT_TRUE(
     decoder.loadDictionariesFromDirectory((std::filesystem::path(FIXDECODER_SOURCE_DIR) / "data/quickfix").string(),
                                           &error))
     << error;

    const auto file_path = std::filesystem::path(FIXDECODER_SOURCE_DIR) / "data/samples/valid" / sample.file_name;
    const auto messages  = readMessageFile(file_path);

    ASSERT_FALSE(messages.empty()) << "No sample messages in " << file_path.string();

    for(const auto &message: messages)
    {
        const std::vector<std::string> invalid_messages = {
         removeTag8(message),
         breakMsgTypeTag(message),
         makeBeginTagNonNumeric(message),
        };

        for(const auto &invalid: invalid_messages)
        {
            const fix::DecodedMessage decoded = decoder.decode(invalid);
            EXPECT_FALSE(looksValidDecode(decoded)) << "Expected invalid decode for: " << invalid;
        }
    }
}

TEST_P(RealisticSubsetMessagesTest, RealisticSubsetDecodes)
{
    const auto sample = GetParam();

    fix::Decoder decoder;
    std::string  error;
    ASSERT_TRUE(
     decoder.loadDictionariesFromDirectory((std::filesystem::path(FIXDECODER_SOURCE_DIR) / "data/quickfix").string(),
                                           &error))
     << error;

    const auto file_path = std::filesystem::path(FIXDECODER_SOURCE_DIR) / "test/unit/test_messages" / sample.file_name;
    const auto messages  = readMessageFile(file_path);

    ASSERT_EQ(messages.size(), 20U) << "Expected 20 realistic messages in " << file_path.string();

    for(const auto &message: messages)
    {
        const fix::DecodedMessage decoded = decoder.decode(message);
        EXPECT_TRUE(looksValidDecode(decoded)) << "Expected valid decode for: " << message;
        EXPECT_EQ(decoded.begin_string, sample.begin_string) << "Unexpected BeginString for: " << message;
        EXPECT_GE(decoded.fields.size(), 6U) << "Unexpectedly short decoded field set for: " << message;
    }
}

INSTANTIATE_TEST_SUITE_P(PerFixVersion,
                         SampleMessagesTest,
                         ::testing::Values(SampleSet{"FIX40.messages", "FIX.4.0"},
                                           SampleSet{"FIX41.messages", "FIX.4.1"},
                                           SampleSet{"FIX42.messages", "FIX.4.2"},
                                           SampleSet{"FIX43.messages", "FIX.4.3"},
                                           SampleSet{"FIX44.messages", "FIX.4.4"},
                                           SampleSet{"FIX50.messages", "FIXT.1.1"},
                                           SampleSet{"FIX50SP1.messages", "FIXT.1.1"},
                                           SampleSet{"FIX50SP2.messages", "FIXT.1.1"},
                                           SampleSet{"FIXT11.messages", "FIXT.1.1"}),
                         [](const testing::TestParamInfo<SampleSet> &info)
                         {
                             std::string name = info.param.file_name;
                             for(char &ch: name)
                             {
                                 if(!std::isalnum(static_cast<unsigned char>(ch)))
                                 {
                                     ch = '_';
                                 }
                             }
                             return name;
                         });

INSTANTIATE_TEST_SUITE_P(PerFixVersionRealisticSubset,
                         RealisticSubsetMessagesTest,
                         ::testing::Values(SampleSet{"FIX40_realistic_20.messages", "FIX.4.0"},
                                           SampleSet{"FIX41_realistic_20.messages", "FIX.4.1"},
                                           SampleSet{"FIX42_realistic_20.messages", "FIX.4.2"},
                                           SampleSet{"FIX43_realistic_20.messages", "FIX.4.3"},
                                           SampleSet{"FIX44_realistic_20.messages", "FIX.4.4"},
                                           SampleSet{"FIX50_realistic_20.messages", "FIXT.1.1"},
                                           SampleSet{"FIX50SP1_realistic_20.messages", "FIXT.1.1"},
                                           SampleSet{"FIX50SP2_realistic_20.messages", "FIXT.1.1"},
                                           SampleSet{"FIXT11_realistic_20.messages", "FIXT.1.1"}),
                         [](const testing::TestParamInfo<SampleSet> &info)
                         {
                             std::string name = info.param.file_name;
                             for(char &ch: name)
                             {
                                 if(!std::isalnum(static_cast<unsigned char>(ch)))
                                 {
                                     ch = '_';
                                 }
                             }
                             return name;
                         });
