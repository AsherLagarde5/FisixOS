#pragma once
#include "engine_types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace engine {

enum class NodeType {
    Document,
    Element,
    Text,
    Comment
};

class DOMNode {
public:
    NodeType type = NodeType::Element;
    DOMNode* parent = nullptr;
    std::vector<std::shared_ptr<DOMNode>> children;

    DOMNode(NodeType t) : type(t) {}
    virtual ~DOMNode() = default;

    virtual std::wstring text_content() const {
        std::wstring res;
        for (const auto& child : children) {
            res += child->text_content();
        }
        return res;
    }
};

class DOMText : public DOMNode {
public:
    std::wstring text;

    DOMText(const std::wstring& t) : DOMNode(NodeType::Text), text(t) {}

    std::wstring text_content() const override {
        return text;
    }
};

class DOMElement : public DOMNode {
public:
    std::wstring tag_name;
    std::map<std::wstring, std::wstring> attributes;

    DOMElement(const std::wstring& tag)
        : DOMNode(NodeType::Element), tag_name(to_lower(tag)) {}

    bool has_attribute(const std::wstring& name) const {
        return attributes.find(to_lower(name)) != attributes.end();
    }

    std::wstring get_attribute(const std::wstring& name, const std::wstring& default_val = L"") const {
        auto it = attributes.find(to_lower(name));
        return (it != attributes.end()) ? it->second : default_val;
    }

    std::wstring id() const {
        return get_attribute(L"id");
    }

    std::vector<std::wstring> class_list() const {
        std::wstring cls = get_attribute(L"class");
        std::vector<std::wstring> list;
        std::wstringstream wss(cls);
        std::wstring item;
        while (wss >> item) {
            list.push_back(to_lower(item));
        }
        return list;
    }

    bool has_class(const std::wstring& cls_name) const {
        std::wstring target = to_lower(cls_name);
        for (const auto& c : class_list()) {
            if (c == target) return true;
        }
        return false;
    }
};

class DOMDocument : public DOMNode {
public:
    std::wstring title = L"Untitled Document";
    std::wstring url;
    std::vector<std::wstring> embedded_stylesheets;
    std::vector<std::wstring> stylesheet_urls;

    DOMDocument() : DOMNode(NodeType::Document) {}

    std::shared_ptr<DOMElement> body() const {
        return find_element_by_tag(L"body");
    }

    std::shared_ptr<DOMElement> head() const {
        return find_element_by_tag(L"head");
    }

    std::shared_ptr<DOMElement> find_element_by_tag(const std::wstring& tag) const {
        return find_tag_recursive(shared_from_this_const(), to_lower(tag));
    }

private:
    std::shared_ptr<const DOMDocument> shared_from_this_const() const {
        // Find in children
        return nullptr;
    }

    static std::shared_ptr<DOMElement> find_tag_recursive(const std::shared_ptr<const DOMNode>& node, const std::wstring& tag) {
        if (!node) return nullptr;
        if (node->type == NodeType::Element) {
            auto elem = std::static_pointer_cast<DOMElement>(std::const_pointer_cast<DOMNode>(node));
            if (elem->tag_name == tag) return elem;
        }
        for (const auto& child : node->children) {
            auto found = find_tag_recursive(child, tag);
            if (found) return found;
        }
        return nullptr;
    }
};

} // namespace engine
