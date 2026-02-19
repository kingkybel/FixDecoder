/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/fix_dictionary.cc
 * Description: FIX dictionary XML parser and lookup implementation.
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

#include "fix_dictionary.h"

#include <filesystem>
#include <sstream>
#include <tinyxml2.h>

namespace fix
{

namespace
{

    MemberKind memberKindFromName(const char *name)
    {
        if(!name)
        {
            return MemberKind::Field;
        }
        if(std::string{name} == "component")
        {
            return MemberKind::Component;
        }
        if(std::string{name} == "group")
        {
            return MemberKind::Group;
        }
        return MemberKind::Field;
    }

    void parseMembers(const tinyxml2::XMLElement *parent, std::vector<Member> &out)
    {
        for(const tinyxml2::XMLElement *child = parent->FirstChildElement(); child; child = child->NextSiblingElement())
        {
            const std::string element_name = child->Name() ? child->Name() : "";
            if(element_name != "field" && element_name != "component" && element_name != "group")
            {
                continue;
            }

            Member member;
            member.kind = memberKindFromName(child->Name());

            if(const char *name_attr = child->Attribute("name"))
            {
                member.name = name_attr;
            }
            member.required = Dictionary::isRequiredAttr(child->Attribute("required"));

            if(member.kind == MemberKind::Group)
            {
                parseMembers(child, member.children);
            }

            out.emplace_back(std::move(member));
        }
    }

}  // namespace

std::string Dictionary::buildBeginString(const std::string &type, int major, int minor)
{
    if(type == "FIXT")
    {
        return "FIXT." + std::to_string(major) + "." + std::to_string(minor);
    }
    return "FIX." + std::to_string(major) + "." + std::to_string(minor);
}

bool Dictionary::isRequiredAttr(const char *value)
{
    static constexpr char upper_y = 0x59;
    static constexpr char lower_y = 0x79;

    return value && (value[0] == upper_y || value[0] == lower_y);
}

bool Dictionary::loadFromFile(const std::string &path, std::string *error)
{
    tinyxml2::XMLDocument doc;
    const auto            status = doc.LoadFile(path.c_str());
    if(status != tinyxml2::XML_SUCCESS)
    {
        if(error)
        {
            *error = "Failed to load XML: " + path;
        }
        return false;
    }

    const tinyxml2::XMLElement *root = doc.FirstChildElement("fix");
    if(!root)
    {
        if(error)
        {
            *error = "Missing <fix> root element in " + path;
        }
        return false;
    }

    fix_type_ = root->Attribute("type") ? root->Attribute("type") : "";
    root->QueryIntAttribute("major", &major_);
    root->QueryIntAttribute("minor", &minor_);
    root->QueryIntAttribute("servicepack", &servicepack_);
    begin_string_ = buildBeginString(fix_type_, major_, minor_);

    const tinyxml2::XMLElement *fields = root->FirstChildElement("fields");
    if(fields)
    {
        for(const tinyxml2::XMLElement *field = fields->FirstChildElement("field"); field;
            field                             = field->NextSiblingElement("field"))
        {
            FieldDef def;
            int      number = 0;
            field->QueryIntAttribute("number", &number);
            def.name = field->Attribute("name") ? field->Attribute("name") : "";
            def.type = field->Attribute("type") ? field->Attribute("type") : "";

            for(const tinyxml2::XMLElement *val = field->FirstChildElement("value"); val;
                val                             = val->NextSiblingElement("value"))
            {
                FieldEnum e;
                e.value       = val->Attribute("enum") ? val->Attribute("enum") : "";
                e.description = val->Attribute("description") ? val->Attribute("description") : "";
                def.enums.emplace_back(std::move(e));
            }

            if(number > 0)
            {
                def.number = static_cast<std::uint32_t>(number);
                fields_.insert_or_assign(def.number, std::move(def));
            }
        }
    }

    const tinyxml2::XMLElement *messages = root->FirstChildElement("messages");
    if(messages)
    {
        for(const tinyxml2::XMLElement *msg = messages->FirstChildElement("message"); msg;
            msg                             = msg->NextSiblingElement("message"))
        {
            MessageDef def;
            def.name     = msg->Attribute("name") ? msg->Attribute("name") : "";
            def.msg_type = msg->Attribute("msgtype") ? msg->Attribute("msgtype") : "";
            def.msg_cat  = msg->Attribute("msgcat") ? msg->Attribute("msgcat") : "";

            parseMembers(msg, def.members);

            if(!def.msg_type.empty())
            {
                std::string msg_type = def.msg_type;
                messages_.insert_or_assign(std::move(msg_type), std::move(def));
            }
        }
    }

    const tinyxml2::XMLElement *components = root->FirstChildElement("components");
    if(components)
    {
        for(const tinyxml2::XMLElement *component = components->FirstChildElement("component"); component;
            component                             = component->NextSiblingElement("component"))
        {
            std::string name = component->Attribute("name") ? component->Attribute("name") : "";
            if(name.empty())
            {
                continue;
            }
            std::vector<Member> members;
            parseMembers(component, members);
            components_.insert_or_assign(std::move(name), std::move(members));
        }
    }

    return true;
}

const FieldDef *Dictionary::fieldByNumber(const std::uint32_t number) const
{
    const auto it = fields_.find(number);
    if(it == fields_.end())
    {
        return nullptr;
    }
    return &it->second;
}

const MessageDef *Dictionary::messageByType(const std::string &msg_type) const
{
    const auto it = messages_.find(msg_type);
    if(it == messages_.end())
    {
        return nullptr;
    }
    return &it->second;
}

bool DictionarySet::loadFromDirectory(const std::string &path, std::string *error)
{
    dictionaries_.clear();
    begin_index_.clear();

    if(!std::filesystem::exists(path))
    {
        if(error)
        {
            *error = "Dictionary path does not exist: " + path;
        }
        return false;
    }

    std::vector<std::string> failures;

    for(const auto &entry: std::filesystem::directory_iterator(path))
    {
        if(!entry.is_regular_file())
        {
            continue;
        }
        if(entry.path().extension() != ".xml")
        {
            continue;
        }

        Dictionary  dict;
        std::string local_error;
        if(!dict.loadFromFile(entry.path().string(), &local_error))
        {
            failures.emplace_back(std::move(local_error));
            continue;
        }

        const std::size_t idx            = dictionaries_.size();
        begin_index_[dict.beginString()] = idx;
        dictionaries_.push_back(std::move(dict));
    }

    if(dictionaries_.empty())
    {
        if(error)
        {
            std::ostringstream oss;
            oss << "No dictionaries loaded from " << path;
            if(!failures.empty())
            {
                oss << ". Errors: ";
                for(std::size_t i = 0; i < failures.size(); ++i)
                {
                    if(i > 0)
                    {
                        oss << "; ";
                    }
                    oss << failures[i];
                }
            }
            *error = oss.str();
        }
        return false;
    }

    return true;
}

const Dictionary *DictionarySet::findByBeginString(const std::string &begin_string) const
{
    const auto it = begin_index_.find(begin_string);
    if(it == begin_index_.end())
    {
        return nullptr;
    }
    return &dictionaries_[it->second];
}

}  // namespace fix
