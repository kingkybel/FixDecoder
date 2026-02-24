/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   include/fix_msgtype_key.h
 * Description: Key extractor for FIX tags in generator mapping.
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

#ifndef FIXDECODER_FIX_MSGTYPE_KEY_H_INCLUDED
#define FIXDECODER_FIX_MSGTYPE_KEY_H_INCLUDED

#include <charconv>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace fix
{

/**
 * @brief Template key extractor for any FIX tag value.
 *
 * Similar to `util::delimited_number_key`, this type is delimiter-driven.
 * It extracts the value of tag `Tag` and copies up to `Width` bytes
 * into the internal hash buffer.
 *
 * @tparam Tag FIX tag number to extract from the message.
 * @tparam DelimiterA First field delimiter.
 * @tparam DelimiterB Second field delimiter.
 * @tparam Width Number of bytes used to build the hash (must be `<= sizeof(size_t)`).
 */
template<int Tag, char DelimiterA = '|', char DelimiterB = 1, std::size_t Width = sizeof(std::size_t)>
struct basic_fix_msg_key
{
    static_assert(Tag > 0, "Tag must be greater than zero");
    static_assert(Width > 0 && Width <= sizeof(std::size_t), "Width must be in [1, sizeof(size_t)]");

    /**
     * @brief Builds a key from a raw FIX message.
     * @param message Raw FIX message using SOH or `|` separators.
     */
    explicit basic_fix_msg_key(std::string_view message) : hash_(0)
    {
        char                   buffer[sizeof(std::size_t)] = {};
        const std::string_view tag_value                   = extractTagValue(message);
        const std::size_t      count                       = (tag_value.size() < Width) ? tag_value.size() : Width;
        if(count > 0)
        {
            std::memcpy(buffer, tag_value.data(), count);
        }
        std::memcpy(&hash_, buffer, Width);
    }

    /**
     * @brief Builds a key from a std::string.
     * @param message Raw FIX message.
     */
    explicit basic_fix_msg_key(const std::string &message) : basic_fix_msg_key(std::string_view{message})
    {
    }

    /**
     * @brief Returns the computed key hash.
     * @return Hash value derived from tag `Tag`.
     */
    std::size_t hash() const
    {
        return hash_;
    }

    private:
    std::size_t hash_;

    static constexpr bool isDelimiter(const char c)
    {
        return c == DelimiterA || c == DelimiterB;
    }

    static std::string_view extractTagValue(const std::string_view message)
    {
        char tag_buffer[12]      = {};
        const auto [end_ptr, ec] = std::to_chars(tag_buffer, tag_buffer + sizeof(tag_buffer), Tag);
        if(ec != std::errc{})
        {
            return std::string_view{};
        }
        const auto tag_len = static_cast<std::size_t>(end_ptr - tag_buffer);

        for(std::size_t i = 0; i < message.size();)
        {
            const std::size_t token_start = i;
            while(i < message.size() && !isDelimiter(message[i]))
            {
                ++i;
            }
            const auto token_end = i;
            if(i < message.size())
            {
                ++i;
            }

            const auto token_len = token_end - token_start;
            if(token_len <= tag_len)
            {
                continue;
            }
            if(message.compare(token_start, tag_len, tag_buffer, tag_len) != 0)
            {
                continue;
            }
            if(message[token_start + tag_len] != '=')
            {
                continue;
            }

            const auto value_start = token_start + tag_len + 1;
            return message.substr(value_start, token_end - value_start);
        }

        return std::string_view{};
    }
};

/**
 * @brief Default key extractor for FIX MsgType (tag 35) using `|` and SOH (`0x01`) delimiters.
 */
using fix_msg_key = basic_fix_msg_key<35, '|', 1>;

}  // namespace fix

#endif  // FIXDECODER_FIX_MSGTYPE_KEY_H_INCLUDED
