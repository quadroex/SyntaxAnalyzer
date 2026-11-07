#include "core.hpp"

#include <cmath>
#include <set>

static void fail(Node* node, const std::string& message) {
    throw Diagnostic("Semantic", node->line, node->column, message);
}

static void requireValue(Type type, Node* node) {
    if (type == Type::Void) fail(node, "A value is required here");
}

static bool constantValue(Node* node, Value& result) {
    try {
        if (node->kind == "Integer" || node->kind == "Real") result = {node->type, std::stod(node->text)};
        else if (node->kind == "Boolean") result = {Type::Bool, node->text == "true" ? 1.0 : 0.0};
        else if (node->kind == "Unary" || node->kind == "Cast") {
            if (!constantValue(node->children[0], result)) return false;
            if (node->kind == "Unary" && node->text == "-") result.number = -result.number;
            if (node->kind == "Unary" && node->text == "!") result.number = result.number == 0 ? 1 : 0;
            result = convert(result, node->type, node);
        }
        else if (node->kind == "Binary") {
            Value left, right;
            if (!constantValue(node->children[0], left)) return false;
            if (node->text == "&&" && left.number == 0) result = {Type::Bool, 0};
            else if (node->text == "||" && left.number != 0) result = {Type::Bool, 1};
            else {
                if (!constantValue(node->children[1], right)) return false;
                result = calculate(node->text, left, right, node);
            }
        }
        else if (node->kind == "Conditional") {
            Value condition;
            if (!constantValue(node->children[0], condition) || !constantValue(node->children[condition.number != 0 ? 1 : 2], result)) return false;
            result = convert(result, node->type, node);
        }
        else return false;
        return true;
    }
    catch (const Diagnostic&) { return false; }
}

Node* Analysis::variable(Node* node) {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        auto found = scope->find(node->text);
        if (found != scope->end()) {
            node->symbol = found->second->symbol;
            node->type = found->second->type;
            node->array = found->second->array;
            node->arraySize = found->second->arraySize;
            return found->second;
        }
    }
    fail(node, "Undeclared variable '" + node->text + "'");
    return nullptr;
}

Type Analysis::scalar(Node* node) {
    Type type = expression(node);
    requireValue(type, node);
    if (node->array) fail(node, "An array cannot be used as a scalar value");
    return type;
}

Type Analysis::lvalue(Node* node) {
    if (node->kind == "Variable") {
        variable(node);
        if (node->array) fail(node, "An array cannot be assigned as a whole");
    }
    else {
        Node* array = variable(node->children[0]);
        if (!array->array) fail(node, "Variable '" + array->text + "' is not an array");
        if (scalar(node->children[1]) == Type::Double) fail(node, "Array index must be integral");
        node->type = array->type;
    }
    return node->type;
}

Type Analysis::expression(Node* node) {
    const std::string& kind = node->kind;
    if (kind == "Integer" || kind == "Real") {
        double number;
        try { number = std::stod(node->text); }
        catch (...) { fail(node, "Numeric literal is out of range"); }
        if (!std::isfinite(number)) fail(node, "Numeric literal is out of range");
        node->type = kind == "Integer" ? Type::Int : Type::Double;
        if (node->type == Type::Int && (number < -2147483648.0 || number > 2147483647.0))
            fail(node, "Integer literal is out of 32-bit range");
    }
    else if (kind == "Boolean") node->type = Type::Bool;
    else if (kind == "Variable") variable(node);
    else if (kind == "Index") lvalue(node);
    else if (kind == "Call") {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            if (scope->count(node->text)) fail(node, "Variable '" + node->text + "' is not callable");
        auto found = functions.find(node->text);
        if (found == functions.end()) fail(node, "Undeclared function '" + node->text + "'");
        if (node->text == "main") fail(node, "Calling main is not allowed");
        Function& function = found->second;
        auto& arguments = node->children[0]->children;
        if (arguments.size() != function.parameters.size()) fail(node, "Wrong number of arguments for '" + node->text + "'");
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            Node* argument = arguments[index];
            if (function.parameterArrays[index]) {
                Type type = expression(argument);
                if (!argument->array || type != function.parameters[index])
                    fail(argument, "Array argument must match the parameter element type");
            }
            else scalar(argument);
        }
        node->symbol = function.symbol;
        node->type = function.type;
    }
    else if (kind == "Unary") {
        Node* child = node->children[0];
        if (node->text == "-" && child->kind == "Integer" && child->text == "2147483648") {
            node->kind = "Integer";
            node->text = "-2147483648";
            node->children.clear();
            node->type = Type::Int;
            return node->type;
        }
        Type type = scalar(child);
        node->type = node->text == "!" ? Type::Bool : (type == Type::Double ? Type::Double : Type::Int);
    }
    else if (kind == "Cast") {
        scalar(node->children[0]);
        node->type = parseType(node->text);
    }
    else if (kind == "Binary") {
        Type left = scalar(node->children[0]);
        Type right = scalar(node->children[1]);
        if (node->text == "%" && (left == Type::Double || right == Type::Double))
            fail(node, "Remainder requires integral operands");
        std::set<std::string> logical = {"<", ">", "<=", ">=", "==", "!=", "&&", "||"};
        node->type = logical.count(node->text) ? Type::Bool :
            (left == Type::Double || right == Type::Double ? Type::Double : Type::Int);
    }
    else if (kind == "Conditional") {
        scalar(node->children[0]);
        Type yes = scalar(node->children[1]);
        Type no = scalar(node->children[2]);
        node->type = yes == no ? yes : (yes == Type::Double || no == Type::Double ? Type::Double : Type::Int);
    }
    else fail(node, "Unsupported expression");
    return node->type;
}

void Analysis::scopedStatement(Node* node) {
    if (node->kind == "Block") statement(node);
    else {
        scopes.emplace_back();
        statement(node);
        scopes.pop_back();
    }
}

void Analysis::statement(Node* node) {
    const std::string& kind = node->kind;
    if (kind == "Block") {
        scopes.emplace_back();
        for (Node* child : node->children) statement(child);
        scopes.pop_back();
    }
    else if (kind == "Declaration") {
        for (Node* child : node->children) {
            if (scopes.back().count(child->text)) fail(child, "Redeclaration of '" + child->text + "'");
            if (child->text == "main") fail(child, "The name main is reserved");
            child->symbol = nextSymbol++;
            scopes.back()[child->text] = child;
            if (child->array) {
                Node* size = child->children[0];
                Node* initializers = child->children[1];
                double count = size->kind == "Empty" ? static_cast<double>(initializers->children.size()) :
                    (size->text.size() > 4 ? 4097.0 : std::stod(size->text));
                if (count < 1 || count > 4096) fail(child, "Array size must be between 1 and 4096");
                child->arraySize = static_cast<int>(count);
                if (size->kind != "Empty") size->type = Type::Int;
                if (initializers->children.size() > static_cast<std::size_t>(child->arraySize))
                    fail(child, "Too many array initializers");
                for (Node* initializer : initializers->children) {
                    Type source = scalar(initializer);
                    Value constant;
                    bool narrowing = (source == Type::Double && child->type != Type::Double) ||
                        (child->type == Type::Double && source != Type::Double && !constantValue(initializer, constant)) ||
                        (child->type == Type::Bool && source == Type::Int &&
                         (!constantValue(initializer, constant) || (constant.number != 0 && constant.number != 1)));
                    if (narrowing) fail(initializer, "Narrowing conversion in array initializer");
                }
            }
            else if (child->children[0]->kind != "Empty") scalar(child->children[0]);
        }
    }
    else if (kind == "Assignment") {
        Type target = lvalue(node->children[0]);
        Type source = scalar(node->children[1]);
        if (node->text == "%=" && (target == Type::Double || source == Type::Double))
            fail(node, "Remainder requires integral operands");
        node->type = target;
    }
    else if (kind == "Update") {
        node->type = lvalue(node->children[0]);
        if (node->type == Type::Bool) fail(node, "Increment and decrement of bool are not supported");
    }
    else if (kind == "ExpressionStatement") {
        expression(node->children[0]);
        if (node->children[0]->array) fail(node, "Bare array expressions are not supported");
    }
    else if (kind == "If") {
        scalar(node->children[0]);
        scopedStatement(node->children[1]);
        if (node->children.size() == 3) scopedStatement(node->children[2]);
    }
    else if (kind == "While" || kind == "For" || kind == "DoWhile") {
        scopes.emplace_back();
        if (kind == "For") {
            statement(node->children[0]);
            if (node->children[1]->kind != "Empty") scalar(node->children[1]);
            statement(node->children[2]);
        }
        else scalar(node->children[0]);
        ++loopDepth;
        Node* body = node->children.back();
        if (kind == "For" && body->kind == "Block") {
            for (Node* child : body->children) statement(child);
        }
        else scopedStatement(body);
        --loopDepth;
        scopes.pop_back();
    }
    else if (kind == "Break" || kind == "Continue") {
        if (loopDepth == 0) fail(node, kind + " is only allowed inside a loop");
    }
    else if (kind == "Return") {
        bool empty = node->children[0]->kind == "Empty";
        if (returnType == Type::Void && !empty) fail(node, "Void function cannot return a value");
        if (returnType != Type::Void && empty) fail(node, "Non-void function must return a value");
        if (!empty) scalar(node->children[0]);
        node->type = returnType;
    }
    else if (kind == "Output") {
        if (!hasIostream) fail(node, "Output requires #include <iostream>");
        if (node->text == "cout" && !usesNamespace) fail(node, "Use std::cout or using namespace std");
        for (Node* child : node->children) {
            if (child->kind == "Endl") {
                if (child->text == "endl" && !usesNamespace) fail(child, "Use std::endl or using namespace std");
            }
            else if (child->kind != "String") {
                if (child->kind == "Binary" && !child->grouped &&
                    child->text != "+" && child->text != "-" && child->text != "*" && child->text != "/" && child->text != "%")
                    fail(child, "Parenthesize comparisons and logical expressions in output");
                if (child->kind == "Conditional" && !child->grouped)
                    fail(child, "Parenthesize conditional expressions in output");
                scalar(child);
            }
        }
    }
    else if (kind == "Input") {
        if (!hasIostream) fail(node, "Input requires #include <iostream>");
        if (node->text == "cin" && !usesNamespace) fail(node, "Use std::cin or using namespace std");
        for (Node* child : node->children) lvalue(child);
    }
    else if (kind != "Empty") fail(node, "Unsupported statement");
}

void Analysis::check(Node* root) {
    for (Node* node : root->children) {
        if (node->kind == "Include") { hasIostream = true; continue; }
        if (node->kind == "UsingNamespace") {
            if (!hasIostream) fail(node, "using namespace std requires #include <iostream>");
            usesNamespace = true;
            continue;
        }
        std::vector<Type> parameters;
        std::vector<bool> parameterArrays;
        std::set<std::string> names;
        for (Node* parameter : node->children[0]->children) {
            if (!names.insert(parameter->text).second) fail(parameter, "Duplicate parameter name");
            parameters.push_back(parameter->type);
            parameterArrays.push_back(parameter->array);
        }
        auto found = functions.find(node->text);
        if (found == functions.end()) {
            functions[node->text] = {node->type, parameters, parameterArrays, nextSymbol++, nullptr};
            found = functions.find(node->text);
        }
        Function& function = found->second;
        if (function.type != node->type || function.parameters != parameters || function.parameterArrays != parameterArrays)
            fail(node, "Conflicting declaration of '" + node->text + "'");
        node->symbol = function.symbol;
        if (node->text == "main" && (node->type != Type::Int || !parameters.empty()))
            fail(node, "main must have type int and no parameters");
        if (node->kind == "Prototype") continue;
        if (function.definition) fail(node, "Redefinition of '" + node->text + "'");
        function.definition = node;
        scopes.clear();
        scopes.emplace_back();
        returnType = node->type;
        for (Node* parameter : node->children[0]->children) {
            parameter->symbol = nextSymbol++;
            scopes.back()[parameter->text] = parameter;
        }
        for (Node* child : node->children[1]->children) statement(child);
    }
    if (!functions.count("main") || !functions.at("main").definition)
        fail(root, "Program requires a definition of int main()");
    for (const auto& entry : functions)
        if (!entry.second.definition) fail(root, "Function '" + entry.first + "' has no definition");
}
