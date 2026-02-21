#include "fix_decoder.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Token
{
    std::uint32_t tag = 0;
    std::string   value;
};

std::string escapeJson(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for(char c: s)
    {
        switch(c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string normalize(std::string raw)
{
    static constexpr char soh = 0x01;
    for(char &c: raw)
    {
        if(c == '|')
        {
            c = soh;
        }
    }
    return raw;
}

bool strictParse(std::string_view message, std::vector<Token> &out, std::string &error)
{
    static constexpr char soh = 0x01;
    auto is_whitespace_only   = [](std::string_view s) {
        for(unsigned char c: s)
        {
            if(!std::isspace(c))
            {
                return false;
            }
        }
        return true;
    };

    std::size_t start       = 0;
    std::size_t token_index = 0;

    while(start < message.size())
    {
        const std::size_t end       = message.find(soh, start);
        const std::size_t token_end = (end == std::string_view::npos) ? message.size() : end;
        const auto        token_view = message.substr(start, token_end - start);
        if(token_view.empty() || is_whitespace_only(token_view))
        {
            if(end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
            continue;
        }

        ++token_index;
        const std::size_t eq_pos = message.find('=', start);
        if(eq_pos == std::string_view::npos || eq_pos >= token_end)
        {
            error = "Token " + std::to_string(token_index) + " is malformed: missing '=' delimiter.";
            return false;
        }

        if(eq_pos == start)
        {
            error = "Token " + std::to_string(token_index) + " has empty tag before '='.";
            return false;
        }

        int tag = 0;
        const auto [ptr, ec] = std::from_chars(message.data() + start, message.data() + eq_pos, tag);
        if(ec != std::errc{} || ptr != message.data() + eq_pos || tag <= 0)
        {
            error = "Token " + std::to_string(token_index) + " has non-numeric or non-positive tag.";
            return false;
        }

        Token t;
        t.tag = static_cast<std::uint32_t>(tag);
        t.value.assign(message.substr(eq_pos + 1, token_end - eq_pos - 1));
        out.push_back(std::move(t));

        if(end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }

    if(out.empty())
    {
        error = "No parseable FIX fields found.";
        return false;
    }

    return true;
}

std::string typedValueToString(const fix::DecodedField &field)
{
    if(const auto *v = std::get_if<bool>(&field.typed_value))
    {
        return *v ? "true" : "false";
    }
    if(const auto *v = std::get_if<std::int64_t>(&field.typed_value))
    {
        return std::to_string(*v);
    }
    if(const auto *v = std::get_if<float>(&field.typed_value))
    {
        std::ostringstream oss;
        oss << *v;
        return oss.str();
    }
    if(const auto *v = std::get_if<double>(&field.typed_value))
    {
        std::ostringstream oss;
        oss << *v;
        return oss.str();
    }
    if(const auto *v = std::get_if<std::string_view>(&field.typed_value))
    {
        return std::string(*v);
    }
    return "<untyped>";
}

}  // namespace

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        std::cerr << "Usage: fix_web_parser <dict_dir> <message>\n";
        return 2;
    }

    const std::string dict_dir = argv[1];
    const std::string raw      = argv[2];
    const std::string norm     = normalize(raw);

    std::vector<Token> tokens;
    std::string        parse_error;
    const bool         strictly_ok = strictParse(norm, tokens, parse_error);

    std::string partial_message;
    for(const auto &t: tokens)
    {
        partial_message += std::to_string(t.tag) + "=" + t.value;
        partial_message.push_back(static_cast<char>(0x01));
    }

    fix::Decoder decoder;
    std::string  load_error;
    decoder.loadDictionariesFromDirectory(dict_dir, &load_error);

    const fix::DecodedMessage decoded = decoder.decode(partial_message);

    bool has_begin = false;
    bool has_type  = false;
    for(const auto &f: decoded.fields)
    {
        if(f.tag == 8)
        {
            has_begin = true;
        }
        else if(f.tag == 35)
        {
            has_type = true;
        }
    }

    if(strictly_ok)
    {
        if(!has_begin)
        {
            parse_error = "Missing required FIX BeginString field (tag 8).";
        }
        else if(!has_type)
        {
            parse_error = "Missing required FIX MsgType field (tag 35).";
        }
    }

    const bool ok = strictly_ok && parse_error.empty();

    std::ostringstream json;
    json << "{";
    json << "\"ok\":" << (ok ? "true" : "false") << ",";
    json << "\"begin_string\":\"" << escapeJson(decoded.begin_string) << "\",";
    json << "\"msg_type\":\"" << escapeJson(decoded.msg_type) << "\",";
    json << "\"parse_error\":\"" << escapeJson(parse_error) << "\",";
    json << "\"structurally_valid\":" << (decoded.structurally_valid ? "true" : "false") << ",";

    json << "\"validation_errors\":[";
    for(std::size_t i = 0; i < decoded.validation_errors.size(); ++i)
    {
        if(i > 0)
        {
            json << ",";
        }
        json << "\"" << escapeJson(decoded.validation_errors[i]) << "\"";
    }
    json << "],";

    json << "\"fields\":[";
    for(std::size_t i = 0; i < decoded.fields.size(); ++i)
    {
        const auto &f = decoded.fields[i];
        if(i > 0)
        {
            json << ",";
        }
        json << "{";
        json << "\"index\":" << (i + 1) << ",";
        json << "\"tag\":" << f.tag << ",";
        json << "\"name\":\"" << escapeJson(f.name) << "\",";
        json << "\"type\":\"" << escapeJson(f.type) << "\",";
        json << "\"value\":\"" << escapeJson(f.value) << "\",";
        json << "\"typed\":\"" << escapeJson(typedValueToString(f)) << "\"";
        json << "}";
    }
    json << "]";

    json << "}";
    std::cout << json.str();
    return 0;
}
