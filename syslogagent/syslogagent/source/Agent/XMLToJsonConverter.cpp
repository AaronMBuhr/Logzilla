#include "stdafx.h"
#include "XmlToJsonConverter.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>

std::string XMLToJSONConverter::escapeJSONString(const std::string& input) {
    std::ostringstream out;
    for (char c : input) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if ('\x00' <= c && c <= '\x1f') {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
            }
            else {
                out << c;
            }
        }
    }
    return out.str();
}

XMLToJSONConverter::XMLNode XMLToJSONConverter::parseXML(const std::string& xml, size_t& pos) {
    XMLNode node;

    // Skip whitespace
    while (pos < xml.size() && std::isspace(xml[pos])) pos++;

    // Parse opening tag
    if (xml[pos] != '<') throw std::runtime_error("Expected '<'");
    pos++;

    // Get tag name
    size_t nameStart = pos;
    while (pos < xml.size() && xml[pos] != ' ' && xml[pos] != '>' && xml[pos] != '/') pos++;
    node.name = xml.substr(nameStart, pos - nameStart);

    // Parse attributes
    while (pos < xml.size() && xml[pos] != '>' && xml[pos] != '/') {
        while (pos < xml.size() && std::isspace(xml[pos])) pos++;
        if (xml[pos] == '>' || xml[pos] == '/') break;

        // Get attribute name
        size_t attrNameStart = pos;
        while (pos < xml.size() && xml[pos] != '=') pos++;
        std::string attrName = xml.substr(attrNameStart, pos - attrNameStart);
        pos++; // Skip '='

        // Get attribute value
        if (xml[pos] != '\'') throw std::runtime_error("Expected '\'");
        pos++;
        size_t attrValueStart = pos;
        while (pos < xml.size() && xml[pos] != '\'') pos++;
        std::string attrValue = xml.substr(attrValueStart, pos - attrValueStart);
        pos++; // Skip closing quote

        node.attributes[attrName] = attrValue;
    }

    // Handle self-closing tags
    if (xml[pos] == '/') {
        pos += 2; // Skip "/>"
        return node;
    }

    // Skip '>'
    if (xml[pos] != '>') throw std::runtime_error("Expected '>'");
    pos++;

    // Parse content and child nodes
    while (pos < xml.size()) {
        // Skip whitespace
        while (pos < xml.size() && std::isspace(xml[pos])) pos++;

        if (xml[pos] == '<') {
            if (xml[pos + 1] == '/') {
                // Closing tag
                pos += 2;
                size_t closeNameStart = pos;
                while (pos < xml.size() && xml[pos] != '>') pos++;
                std::string closeName = xml.substr(closeNameStart, pos - closeNameStart);
                if (closeName != node.name) throw std::runtime_error("Mismatched closing tag");
                pos++; // Skip '>'
                break;
            }
            else {
                // Child node
                node.children.push_back(parseXML(xml, pos));
            }
        }
        else {
            // Text content
            size_t textStart = pos;
            while (pos < xml.size() && xml[pos] != '<') pos++;
            std::string text = xml.substr(textStart, pos - textStart);
            if (!text.empty()) {
                node.text += text;
            }
        }
    }

    return node;
}

std::string XMLToJSONConverter::nodeToJSON(const XMLNode& node) {
    std::ostringstream json;
    json << "{";

    // Add xmlns if present
    auto xmlns = node.attributes.find("xmlns");
    if (xmlns != node.attributes.end()) {
        json << "\"@xmlns\":\"" << escapeJSONString(xmlns->second) << "\"";
        if (!node.attributes.empty() || !node.children.empty() || !node.text.empty()) {
            json << ",";
        }
    }

    // Add other attributes
    bool firstAttr = true;
    for (const auto& attr : node.attributes) {
        if (attr.first != "xmlns") {
            if (!firstAttr) json << ",";
            json << "\"@" << attr.first << "\":\"" << escapeJSONString(attr.second) << "\"";
            firstAttr = false;
        }
    }

    // Add children
    if (!node.children.empty()) {
        if (!node.attributes.empty()) json << ",";

        std::map<std::string, std::vector<XMLNode>> groupedChildren;
        for (const auto& child : node.children) {
            groupedChildren[child.name].push_back(child);
        }

        bool firstChild = true;
        for (const auto& group : groupedChildren) {
            if (!firstChild) json << ",";
            json << "\"" << group.first << "\":";

            if (group.second.size() == 1) {
                json << nodeToJSON(group.second[0]);
            }
            else {
                json << "[";
                for (size_t i = 0; i < group.second.size(); i++) {
                    if (i > 0) json << ",";
                    json << nodeToJSON(group.second[i]);
                }
                json << "]";
            }
            firstChild = false;
        }
    }

    // Add text content if present and not empty
    std::string trimmedText = node.text;
    trimmedText.erase(0, trimmedText.find_first_not_of(" \n\r\t"));
    trimmedText.erase(trimmedText.find_last_not_of(" \n\r\t") + 1);

    if (!trimmedText.empty()) {
        if (!node.attributes.empty() || !node.children.empty()) json << ",";
        json << "\"#text\":\"" << escapeJSONString(trimmedText) << "\"";
    }

    json << "}";
    return json.str();
}

std::string XMLToJSONConverter::convert(const std::string& xml) {
    try {
        size_t pos = 0;
        XMLNode root = parseXML(xml, pos);
        return nodeToJSON(root);
    }
    catch (const std::exception& e) {
        return "{\"error\":\"" + escapeJSONString(e.what()) + "\"}";
    }
}

std::string XMLToJSONConverter::convert(const char* utf8Xml) {
    if (!utf8Xml) {
        return "{\"error\":\"Input is null\"}";
    }
    return convert(std::string(utf8Xml));
}

std::string XMLToJSONConverter::convert(const char* utf8Xml, size_t bufferSize) {
    if (!utf8Xml) {
        return "{\"error\":\"Input is null\"}";
    }
    return convert(std::string(utf8Xml, bufferSize));
}