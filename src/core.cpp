#include "core.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

Diagnostic::Diagnostic(std::string stage, int line, int column, const std::string& message)
    : std::runtime_error(message), stage(std::move(stage)), line(line), column(column) {}

Node* Context::make(const std::string& kind, const std::string& text, int line, int column,
                    std::initializer_list<Node*> children) {
    auto node = std::make_unique<Node>();
    node->id = static_cast<int>(nodes.size());
    node->kind = kind;
    node->text = text;
    node->line = line;
    node->column = column;
    node->children = children;
    Node* result = node.get();
    nodes.push_back(std::move(node));
    return result;
}

Node* Context::declaration(const std::string& type, Node* list) {
    for (Node* node : list->children) node->type = parseType(type);
    return list;
}

Node* Context::function(const std::string& type, const std::string& name, Node* parameters,
                        Node* body, int line, int column) {
    Node* node = make(body ? "Function" : "Prototype", name, line, column, {parameters});
    node->type = parseType(type);
    if (body) node->children.push_back(body);
    return node;
}

std::string typeName(Type type) {
    if (type == Type::Int) return "int";
    if (type == Type::Double) return "double";
    if (type == Type::Bool) return "bool";
    if (type == Type::Void) return "void";
    return "unknown";
}

Type parseType(const std::string& name) {
    if (name == "int") return Type::Int;
    if (name == "double") return Type::Double;
    if (name == "bool") return Type::Bool;
    if (name == "void") return Type::Void;
    return Type::Unknown;
}

std::string quote(const std::string& text) {
    std::ostringstream result;
    result << '"';
    for (unsigned char character : text) {
        if (character == '"') result << "\\\"";
        else if (character == '\\') result << "\\\\";
        else if (character == '\n') result << "\\n";
        else if (character == '\r') result << "\\r";
        else if (character == '\t') result << "\\t";
        else if (character < 32) result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character) << std::dec;
        else result << character;
    }
    result << '"';
    return result.str();
}

std::string decodeString(const std::string& text, int line, int column) {
    std::string result;
    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
        char character = text[index];
        if (character != '\\') result += character;
        else {
            char escaped = text[++index];
            if (escaped == 'n') result += '\n';
            else if (escaped == 'r') result += '\r';
            else if (escaped == 't') result += '\t';
            else if (escaped == '\\' || escaped == '"' || escaped == '\'') result += escaped;
            else throw Diagnostic("Lexical", line, column, "Unsupported escape sequence");
        }
    }
    return result;
}

std::string astJson(Node* node) {
    std::ostringstream result;
    result << "{\"id\":" << node->id << ",\"kind\":" << quote(node->kind)
           << ",\"text\":" << quote(node->text) << ",\"type\":" << quote(typeName(node->type) + (node->array ? "[]" : ""))
           << ",\"line\":" << node->line << ",\"column\":" << node->column
           << ",\"symbol\":" << node->symbol << ",\"array\":" << (node->array ? "true" : "false")
           << ",\"size\":" << node->arraySize << ",\"children\":[";
    for (std::size_t index = 0; index < node->children.size(); ++index) {
        if (index) result << ',';
        result << astJson(node->children[index]);
    }
    result << "]}";
    return result.str();
}

std::string formatValue(Value value) {
    if (value.type == Type::Void) return "void";
    if (value.type == Type::Bool) return value.number ? "1" : "0";
    if (value.type == Type::Int) return std::to_string(static_cast<std::int64_t>(value.number));
    std::ostringstream result;
    result << std::setprecision(6) << std::defaultfloat << value.number;
    return result.str();
}

Value convert(Value value, Type type, Node* node) {
    if (type == Type::Void || value.type == Type::Void || value.elements)
        throw Diagnostic("Runtime", node->line, node->column, "Cannot convert void value");
    if (!std::isfinite(value.number))
        throw Diagnostic("Runtime", node->line, node->column, "Non-finite numeric result");
    if (type == Type::Bool) return {type, value.number != 0 ? 1.0 : 0.0};
    if (type == Type::Int) {
        double integer = std::trunc(value.number);
        if (integer < -2147483648.0 || integer > 2147483647.0)
            throw Diagnostic("Runtime", node->line, node->column, "Integer overflow");
        return {type, integer};
    }
    return {type, value.number};
}

Value calculate(const std::string& operation, Value left, Value right, Node* node) {
    double a = left.number;
    double b = right.number;
    if (operation == "<") return {Type::Bool, a < b ? 1.0 : 0.0};
    if (operation == ">") return {Type::Bool, a > b ? 1.0 : 0.0};
    if (operation == "<=") return {Type::Bool, a <= b ? 1.0 : 0.0};
    if (operation == ">=") return {Type::Bool, a >= b ? 1.0 : 0.0};
    if (operation == "==") return {Type::Bool, a == b ? 1.0 : 0.0};
    if (operation == "!=") return {Type::Bool, a != b ? 1.0 : 0.0};
    if (operation == "&&") return {Type::Bool, a != 0 && b != 0 ? 1.0 : 0.0};
    if (operation == "||") return {Type::Bool, a != 0 || b != 0 ? 1.0 : 0.0};
    Type type = left.type == Type::Double || right.type == Type::Double ? Type::Double : Type::Int;
    if ((operation == "/" || operation == "%") && b == 0)
        throw Diagnostic("Runtime", node->line, node->column, "Division by zero");
    double result = 0;
    if (operation == "+") result = a + b;
    else if (operation == "-") result = a - b;
    else if (operation == "*") result = a * b;
    else if (operation == "/") result = type == Type::Int ? std::trunc(a / b) : a / b;
    else if (operation == "%") {
        if (a == -2147483648.0 && b == -1)
            throw Diagnostic("Runtime", node->line, node->column, "Integer overflow");
        result = std::fmod(a, b);
    }
    else throw Diagnostic("Runtime", node->line, node->column, "Unknown operation");
    return convert({type, result}, type, node);
}
