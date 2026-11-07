#include "core.hpp"

#include <cmath>

Interpreter::Interpreter(Analysis& analysis, const std::string& inputText) : analysis(analysis), input(inputText) {}

Value& Interpreter::target(Node* node) {
    if (node->kind == "Variable") return frames.back().at(node->symbol);
    auto elements = frames.back().at(node->children[0]->symbol).elements;
    int index = static_cast<int>(expression(node->children[1]).number);
    if (index < 0 || index >= static_cast<int>(elements->size()))
        throw Diagnostic("Runtime", node->line, node->column, "Array index out of bounds");
    return elements->at(index);
}

Value Interpreter::readInput(Node* node) {
    std::string word;
    if (!(input >> word)) throw Diagnostic("Runtime", node->line, node->column, "Input exhausted");
    std::istringstream parser(word);
    Value value{node->type, 0};
    if (node->type == Type::Double) {
        if (!(parser >> value.number) || parser.peek() != std::char_traits<char>::eof() || !std::isfinite(value.number))
            throw Diagnostic("Runtime", node->line, node->column, "Invalid numeric input");
    }
    else {
        std::int64_t number;
        if (!(parser >> number) || parser.peek() != std::char_traits<char>::eof() || number < -2147483648LL || number > 2147483647LL ||
            (node->type == Type::Bool && number != 0 && number != 1))
            throw Diagnostic("Runtime", node->line, node->column, "Invalid numeric input");
        value.number = static_cast<double>(number);
    }
    return value;
}

void Interpreter::tick(Node* node) {
    if (++steps > 100000)
        throw Diagnostic("Runtime", node->line, node->column, "Execution step limit exceeded");
}

Value Interpreter::expression(Node* node) {
    tick(node);
    const std::string& kind = node->kind;
    if (kind == "Integer" || kind == "Real") return {node->type, std::stod(node->text)};
    if (kind == "Boolean") return {Type::Bool, node->text == "true" ? 1.0 : 0.0};
    if (kind == "Variable" || kind == "Index") {
        Value value = target(node);
        if (!value.initialized)
            throw Diagnostic("Runtime", node->line, node->column, "Read of uninitialized variable '" + node->text + "'");
        return value;
    }
    if (kind == "Conditional") {
        bool condition = expression(node->children[0]).number != 0;
        return convert(expression(node->children[condition ? 1 : 2]), node->type, node);
    }
    if (kind == "Cast") return convert(expression(node->children[0]), node->type, node);
    if (kind == "Unary") {
        Value value = expression(node->children[0]);
        if (node->text == "!") return {Type::Bool, value.number == 0 ? 1.0 : 0.0};
        if (node->text == "-") value.number = -value.number;
        return convert(value, node->type, node);
    }
    if (kind == "Binary") {
        Value left = expression(node->children[0]);
        if (node->text == "&&" && left.number == 0) return {Type::Bool, 0};
        if (node->text == "||" && left.number != 0) return {Type::Bool, 1};
        Value right = expression(node->children[1]);
        return calculate(node->text, left, right, node);
    }
    if (kind == "Call") {
        std::vector<Value> arguments;
        for (Node* argument : node->children[0]->children) arguments.push_back(expression(argument));
        return call(analysis.functions.at(node->text), arguments, node);
    }
    throw Diagnostic("Runtime", node->line, node->column, "Unsupported expression");
}

Flow Interpreter::statement(Node* node) {
    tick(node);
    const std::string& kind = node->kind;
    if (kind == "Block") {
        for (Node* child : node->children) {
            Flow flow = statement(child);
            if (!flow.kind.empty()) return flow;
        }
    }
    else if (kind == "Declaration") {
        for (Node* child : node->children) {
            if (child->array) {
                int previous = allocations.back()[child->symbol];
                if (arrayElements - previous + child->arraySize > 65536)
                    throw Diagnostic("Runtime", child->line, child->column, "Array storage limit exceeded");
                arrayElements += child->arraySize - previous;
                allocations.back()[child->symbol] = child->arraySize;
                Value array{child->type, 0};
                array.elements = std::make_shared<std::vector<Value>>(child->arraySize, Value{child->type, 0, false});
                frames.back()[child->symbol] = array;
                Node* initializers = child->children[1];
                if (initializers->kind != "Empty") {
                    std::size_t index = 0;
                    for (Node* initializer : initializers->children)
                        array.elements->at(index++) = convert(expression(initializer), child->type, initializer);
                    while (index < array.elements->size()) array.elements->at(index++) = {child->type, 0};
                }
                continue;
            }
            frames.back()[child->symbol] = {child->type, 0, false};
            if (child->children[0]->kind != "Empty")
                frames.back()[child->symbol] = convert(expression(child->children[0]), child->type, child);
        }
    }
    else if (kind == "Assignment") {
        Node* destination = node->children[0];
        Value value = expression(node->children[1]);
        if (node->text != "=") tick(destination);
        Value& slot = target(destination);
        if (node->text != "=") {
            if (!slot.initialized)
                throw Diagnostic("Runtime", destination->line, destination->column, "Read of uninitialized variable '" + destination->text + "'");
            value = calculate(node->text.substr(0, 1), slot, value, node);
        }
        slot = convert(value, destination->type, node);
    }
    else if (kind == "Update") {
        Node* destination = node->children[0];
        tick(destination);
        Value& slot = target(destination);
        if (!slot.initialized)
            throw Diagnostic("Runtime", destination->line, destination->column, "Read of uninitialized variable '" + destination->text + "'");
        slot = convert(calculate(node->text == "++" ? "+" : "-", slot, {Type::Int, 1}, node), destination->type, node);
    }
    else if (kind == "ExpressionStatement") expression(node->children[0]);
    else if (kind == "If") {
        if (expression(node->children[0]).number != 0) return statement(node->children[1]);
        if (node->children.size() == 3) return statement(node->children[2]);
    }
    else if (kind == "DoWhile") {
        while (true) {
            tick(node);
            Flow flow = statement(node->children[1]);
            if (flow.kind == "Return") return flow;
            if (flow.kind == "Break") break;
            if (expression(node->children[0]).number == 0) break;
        }
    }
    else if (kind == "While" || kind == "For") {
        bool isFor = kind == "For";
        if (isFor) statement(node->children[0]);
        Node* condition = node->children[isFor ? 1 : 0];
        while (true) {
            tick(node);
            if (condition->kind != "Empty" && expression(condition).number == 0) break;
            Flow flow = statement(node->children.back());
            if (flow.kind == "Return") return flow;
            if (flow.kind == "Break") break;
            if (isFor) statement(node->children[2]);
        }
    }
    else if (kind == "Break" || kind == "Continue") return {kind, {}};
    else if (kind == "Return") {
        Value value;
        if (node->children[0]->kind != "Empty") value = convert(expression(node->children[0]), node->type, node);
        return {"Return", value};
    }
    else if (kind == "Output") {
        for (Node* child : node->children) {
            if (child->kind == "String") output += child->text;
            else if (child->kind == "Endl") output += '\n';
            else output += formatValue(expression(child));
            if (output.size() > 65536)
                throw Diagnostic("Runtime", node->line, node->column, "Output limit exceeded");
        }
    }
    else if (kind == "Input") {
        for (Node* child : node->children) {
            Value& slot = target(child);
            slot = readInput(child);
        }
    }
    return {};
}

Value Interpreter::call(const Function& function, const std::vector<Value>& arguments, Node* site) {
    if (frames.size() >= 128)
        throw Diagnostic("Runtime", site->line, site->column, "Call depth limit exceeded");
    Node* definition = function.definition;
    frames.emplace_back();
    allocations.emplace_back();
    auto& parameters = definition->children[0]->children;
    for (std::size_t index = 0; index < parameters.size(); ++index)
        frames.back()[parameters[index]->symbol] = parameters[index]->array ? arguments[index] : convert(arguments[index], parameters[index]->type, site);
    Flow flow = statement(definition->children[1]);
    for (const auto& allocation : allocations.back()) arrayElements -= allocation.second;
    allocations.pop_back();
    frames.pop_back();
    if (flow.kind == "Return") return flow.value;
    if (definition->text == "main") return {Type::Int, 0};
    if (function.type == Type::Void) return {};
    throw Diagnostic("Runtime", definition->line, definition->column, "Function '" + definition->text + "' ended without returning a value");
}

Value Interpreter::run(Node* root) {
    return call(analysis.functions.at("main"), {}, root);
}
