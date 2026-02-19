/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   include/fix_dictionary.h
 * Description: FIX dictionary model and loading interfaces.
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

#ifndef FIXDECODER_FIX_DICTIONARY_H_INCLUDED
#define FIXDECODER_FIX_DICTIONARY_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fix
{

/**
 * @brief Enumerated value metadata for a FIX field.
 */
struct FieldEnum
{
    /** Raw enum value as stored in FIX messages. */
    std::string value;
    /** Human-readable enum description from dictionary metadata. */
    std::string description;
};

/**
 * @brief Definition of a FIX field from dictionary XML.
 */
struct FieldDef
{
    /** Numeric field tag. */
    std::uint32_t number = 0;
    /** Field name (for example `MsgType`). */
    std::string name;
    /** Field type string from dictionary (for example `STRING`, `INT`). */
    std::string type;
    /** Optional enum values defined for the field. */
    std::vector<FieldEnum> enums;
};

/**
 * @brief Type of message member in a FIX message definition.
 */
enum class MemberKind
{
    /** Simple field reference. */
    Field,
    /** Reusable component reference. */
    Component,
    /** Repeating group definition. */
    Group
};

/**
 * @brief A member entry in a message or component definition.
 */
struct Member
{
    /** Member kind (field, component, or group). */
    MemberKind kind = MemberKind::Field;
    /** Member name as defined in dictionary XML. */
    std::string name;
    /** Indicates whether the member is required (`Y`). */
    bool required = false;
    /** Nested group members (used when kind is `Group`). */
    std::vector<Member> children;
};

/**
 * @brief Definition of a FIX message type from dictionary XML.
 */
struct MessageDef
{
    /** Human-readable message name. */
    std::string name;
    /** Message type code (tag 35 value). */
    std::string msg_type;
    /** Message category (`admin` or `app`). */
    std::string msg_cat;
    /** Ordered members defined for this message. */
    std::vector<Member> members;
};

/**
 * @brief Represents a single FIX dictionary loaded from XML.
 */
class Dictionary
{
    public:
    /**
     * @brief Loads one QuickFIX-compatible XML dictionary file.
     * @param path Path to dictionary XML file.
     * @param error Optional output parameter for a human-readable error message.
     * @return `true` if loading succeeded, `false` otherwise.
     */
    bool loadFromFile(const std::string &path, std::string *error = nullptr);

    /**
     * @brief Finds a field definition by numeric tag.
     * @param number FIX tag number.
     * @return Pointer to field definition, or `nullptr` if not found.
     */
    const FieldDef *fieldByNumber(std::uint32_t number) const;
    /**
     * @brief Finds a message definition by message type code.
     * @param msg_type FIX MsgType value (tag 35).
     * @return Pointer to message definition, or `nullptr` if not found.
     */
    const MessageDef *messageByType(const std::string &msg_type) const;

    /**
     * @brief Returns the dictionary begin string (for example `FIX.4.4`).
     * @return Dictionary begin string.
     */
    const std::string &beginString() const
    {
        return begin_string_;
    }
    /**
     * @brief Returns the dictionary transport type (for example `FIX` or `FIXT`).
     * @return Dictionary type string.
     */
    const std::string &type() const
    {
        return fix_type_;
    }

    /**
     * @brief Converts a QuickFIX `required` attribute value to boolean.
     * @param value XML attribute value pointer.
     * @return `true` when attribute starts with `Y` or `y`, otherwise `false`.
     */
    static bool isRequiredAttr(const char *value);

    private:
    std::string begin_string_;
    std::string fix_type_;
    int         major_       = 0;
    int         minor_       = 0;
    int         servicepack_ = 0;

    std::unordered_map<std::uint32_t, FieldDef>          fields_;
    std::unordered_map<std::string, MessageDef>          messages_;
    std::unordered_map<std::string, std::vector<Member>> components_;

    static std::string buildBeginString(const std::string &type, int major, int minor);
};

/**
 * @brief Collection of dictionaries indexed by begin string.
 */
class DictionarySet
{
    public:
    /**
     * @brief Loads all dictionary XML files from a directory.
     * @param path Directory containing dictionary files.
     * @param error Optional output parameter for a human-readable error message.
     * @return `true` if at least one dictionary was loaded, `false` otherwise.
     */
    bool loadFromDirectory(const std::string &path, std::string *error = nullptr);

    /**
     * @brief Finds a dictionary by begin string.
     * @param begin_string Begin string such as `FIX.4.2` or `FIXT.1.1`.
     * @return Pointer to dictionary, or `nullptr` if not found.
     */
    const Dictionary *findByBeginString(const std::string &begin_string) const;

    private:
    std::vector<Dictionary>                      dictionaries_;
    std::unordered_map<std::string, std::size_t> begin_index_;
};

}  // namespace fix

#endif  // FIXDECODER_FIX_DICTIONARY_H_INCLUDED
