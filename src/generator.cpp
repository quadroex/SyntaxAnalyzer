#include "core.hpp"

#include <sstream>

static std::string variableName(Node* node) {
    return "v" + std::to_string(node->symbol);
}

static std::string cast(const std::string& value, Type type, int line) {
    return "__cast(" + value + ", " + quote(typeName(type)) + ", " + std::to_string(line) + ")";
}

static std::string expressionCode(Node* node) {
    std::string result;
    std::string line = std::to_string(node->line);
    const std::string& kind = node->kind;
    if (kind == "Integer" || kind == "Real") result = node->text;
    else if (kind == "Boolean") result = node->text == "true" ? "1" : "0";
    else if (kind == "Variable") result = "__read(" + variableName(node) + ", " + line + ")";
    else if (kind == "Index") result = "__readIndex(" + variableName(node->children[0]) + ", " + expressionCode(node->children[1]) + ", " + line + ")";
    else if (kind == "Conditional") result = cast("(" + expressionCode(node->children[0]) + " ? " + expressionCode(node->children[1]) + " : " + expressionCode(node->children[2]) + ")", node->type, node->line);
    else if (kind == "Cast") result = cast(expressionCode(node->children[0]), node->type, node->line);
    else if (kind == "Unary") {
        std::string value = expressionCode(node->children[0]);
        if (node->text == "!") result = "(" + value + " === 0 ? 1 : 0)";
        else result = cast("(" + node->text + value + ")", node->type, node->line);
    }
    else if (kind == "Binary") {
        std::string left = expressionCode(node->children[0]);
        std::string right = expressionCode(node->children[1]);
        if (node->text == "&&") result = "Number(Boolean(" + left + ") && Boolean(" + right + "))";
        else if (node->text == "||") result = "Number(Boolean(" + left + ") || Boolean(" + right + "))";
        else result = "__binary(" + quote(node->text) + ", " + left + ", " + right + ", " + quote(typeName(node->type)) + ", " + line + ")";
    }
    else if (kind == "Call") {
        result = "f" + std::to_string(node->symbol) + "(";
        auto& arguments = node->children[0]->children;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index) result += ", ";
            result += expressionCode(arguments[index]);
        }
        result += ")";
    }
    return "(__tick(" + line + "), " + result + ")";
}

static std::string updateCode(Node* node) {
    Node* target = node->children[0];
    std::string operation = node->kind == "Update" ? (node->text == "++" ? "+" : "-") : node->text.substr(0, 1);
    std::string right = node->kind == "Update" ? "1" : expressionCode(node->children[1]);
    Type promoted = target->type;
    if (node->kind == "Assignment" && node->children[1]->type == Type::Double) promoted = Type::Double;
    if (promoted == Type::Bool) promoted = Type::Int;
    std::string line = std::to_string(node->line);
    if (target->kind == "Index") {
        std::string index = expressionCode(target->children[1]);
        if (operation != "=") index = "(__tick(" + std::to_string(target->line) + "), " + index + ")";
        return "__assignIndex(" + right + ", " + variableName(target->children[0]) + ", " + index + ", " + quote(operation) + ", " +
            quote(typeName(target->type)) + ", " + quote(typeName(promoted)) + ", " + line + ")";
    }
    if (operation == "=") return variableName(target) + " = " + cast(right, target->type, node->line);
    std::string value = "__binary(" + quote(operation) + ", " + expressionCode(target) + ", __right, " + quote(typeName(promoted)) + ", " + line + ")";
    return "((__right) => " + variableName(target) + " = " + cast(value, target->type, node->line) + ")(" + right + ")";
}

static std::string inlineCode(Node* node) {
    std::string result = "__tick(" + std::to_string(node->line) + ")";
    if (node->kind == "Assignment" || node->kind == "Update") result += ", " + updateCode(node);
    else if (node->kind == "ExpressionStatement") result += ", " + expressionCode(node->children[0]);
    return "(" + result + ")";
}

static void statementCode(Node* node, std::ostringstream& result, int level) {
    std::string indent(level * 4, ' ');
    std::string line = std::to_string(node->line);
    const std::string& kind = node->kind;
    result << indent << "__tick(" << line << ");\n";
    if (kind == "Block") {
        result << indent << "{\n";
        for (Node* child : node->children) statementCode(child, result, level + 1);
        result << indent << "}\n";
    }
    else if (kind == "Declaration") {
        for (Node* child : node->children) {
            if (child->array) {
                result << indent << "let " << variableName(child) << " = __array(" << child->arraySize << ", " << child->symbol << ", " << child->line << ");\n";
                Node* initializers = child->children[1];
                if (initializers->kind != "Empty") {
                    std::size_t index = 0;
                    for (Node* initializer : initializers->children)
                        result << indent << variableName(child) << '[' << index++ << "] = " << cast(expressionCode(initializer), child->type, initializer->line) << ";\n";
                    result << indent << variableName(child) << ".fill(0, " << index << ");\n";
                }
                continue;
            }
            result << indent << "let " << variableName(child) << ";\n";
            if (child->children[0]->kind != "Empty")
                result << indent << variableName(child) << " = " << cast(expressionCode(child->children[0]), child->type, child->line) << ";\n";
        }
    }
    else if (kind == "Assignment" || kind == "Update") result << indent << updateCode(node) << ";\n";
    else if (kind == "ExpressionStatement") result << indent << expressionCode(node->children[0]) << ";\n";
    else if (kind == "If") {
        result << indent << "if (" << expressionCode(node->children[0]) << ") {\n";
        statementCode(node->children[1], result, level + 1);
        result << indent << "}";
        if (node->children.size() == 3) {
            result << " else {\n";
            statementCode(node->children[2], result, level + 1);
            result << indent << "}";
        }
        result << '\n';
    }
    else if (kind == "While") {
        result << indent << "while ((__tick(" << line << "), " << expressionCode(node->children[0]) << ")) {\n";
        statementCode(node->children[1], result, level + 1);
        result << indent << "}\n";
    }
    else if (kind == "DoWhile") {
        result << indent << "do {\n" << indent << "    __tick(" << line << ");\n";
        statementCode(node->children[1], result, level + 1);
        result << indent << "} while (" << expressionCode(node->children[0]) << ");\n";
    }
    else if (kind == "For") {
        result << indent << "{\n";
        statementCode(node->children[0], result, level + 1);
        std::string condition = node->children[1]->kind == "Empty" ? "1" : expressionCode(node->children[1]);
        result << indent << "    for (; (__tick(" << line << "), " << condition << "); " << inlineCode(node->children[2]) << ") {\n";
        statementCode(node->children[3], result, level + 2);
        result << indent << "    }\n" << indent << "}\n";
    }
    else if (kind == "Break") result << indent << "break;\n";
    else if (kind == "Continue") result << indent << "continue;\n";
    else if (kind == "Return") {
        result << indent << "return";
        if (node->children[0]->kind != "Empty") result << ' ' << cast(expressionCode(node->children[0]), node->type, node->line);
        result << ";\n";
    }
    else if (kind == "Output") {
        for (Node* child : node->children) {
            std::string value;
            if (child->kind == "String") value = quote(child->text);
            else if (child->kind == "Endl") value = "\"\\n\"";
            else value = "__format(" + expressionCode(child) + ", " + quote(typeName(child->type)) + ")";
            result << indent << "__write(" << value << ", " << line << ");\n";
        }
    }
    else if (kind == "Input") {
        for (Node* target : node->children) {
            if (target->kind == "Variable")
                result << indent << variableName(target) << " = __input(" << quote(typeName(target->type)) << ", " << target->line << ");\n";
            else
                result << indent << "__inputIndex(" << variableName(target->children[0]) << ", " << expressionCode(target->children[1]) << ", " << quote(typeName(target->type)) << ", " << target->line << ");\n";
        }
    }
}

static bool usesInput(Node* node) {
    if (node->kind == "Input") return true;
    for (Node* child : node->children) if (usesInput(child)) return true;
    return false;
}

std::string generateJavaScript(Node* root) {
    std::ostringstream result;
    result << "\"use strict\";\nconst __needsInput = " << (usesInput(root) ? "true" : "false") << ";\n";
    result << R"JS(

function runMiniCpp(inputText = "") {
    let __output = "";
    let __outputSize = 0;
    let __steps = 0;
    let __depth = 0;
    let __arrayElements = 0;
    const __allocations = [];
    const __inputTokens = inputText.split(/[ \t\r\n\v\f]+/).filter(Boolean);
    let __inputPosition = 0;
    function __fail(message, line) {
        const error = new Error(message);
        error.line = line;
        throw error;
    }
    function __tick(line) {
        if (++__steps > 100000) __fail("Execution step limit exceeded", line);
    }
    function __enter(line) {
        if (__depth >= 128) __fail("Call depth limit exceeded", line);
        ++__depth;
        __allocations.push(new Map());
    }
    function __leave() {
        for (const size of __allocations.pop().values()) __arrayElements -= size;
        --__depth;
    }
    function __array(size, symbol, line) {
        const frame = __allocations[__allocations.length - 1];
        const previous = frame.get(symbol) || 0;
        if (__arrayElements - previous + size > 65536) __fail("Array storage limit exceeded", line);
        __arrayElements += size - previous;
        frame.set(symbol, size);
        return new Array(size);
    }
    function __index(array, index, line) {
        if (!Number.isInteger(index) || index < 0 || index >= array.length) __fail("Array index out of bounds", line);
        return index;
    }
    function __readIndex(array, index, line) {
        return __read(array[__index(array, index, line)], line);
    }
    function __assignIndex(right, array, index, operation, type, promoted, line) {
        __index(array, index, line);
        const value = operation === "=" ? right : __binary(operation, __read(array[index], line), right, promoted, line);
        array[index] = __cast(value, type, line);
        return array[index];
    }
    function __input(type, line) {
        if (__inputPosition >= __inputTokens.length) __fail("Input exhausted", line);
        const word = __inputTokens[__inputPosition++];
        const pattern = type === "double" ? /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/ : /^[+-]?\d+$/;
        const value = Number(word);
        if (!pattern.test(word) || !Number.isFinite(value) ||
            (type !== "double" && (value < -2147483648 || value > 2147483647)) ||
            (type === "bool" && value !== 0 && value !== 1)) __fail("Invalid numeric input", line);
        return value;
    }
    function __inputIndex(array, index, type, line) {
        __index(array, index, line);
        array[index] = __input(type, line);
    }
    function __read(value, line) {
        if (value === undefined) __fail("Read of uninitialized variable", line);
        return value;
    }
    function __cast(value, type, line) {
        if (!Number.isFinite(value)) __fail("Non-finite numeric result", line);
        if (type === "bool") return value !== 0 ? 1 : 0;
        if (type === "int") {
            const integer = Math.trunc(value);
            if (integer < -2147483648 || integer > 2147483647) __fail("Integer overflow", line);
            return integer;
        }
        return value;
    }
    function __binary(operation, a, b, type, line) {
        if (operation === "<") return Number(a < b);
        if (operation === ">") return Number(a > b);
        if (operation === "<=") return Number(a <= b);
        if (operation === ">=") return Number(a >= b);
        if (operation === "==") return Number(a === b);
        if (operation === "!=") return Number(a !== b);
        if ((operation === "/" || operation === "%") && b === 0) __fail("Division by zero", line);
        if (operation === "%" && a === -2147483648 && b === -1) __fail("Integer overflow", line);
        let value;
        if (operation === "+") value = a + b;
        else if (operation === "-") value = a - b;
        else if (operation === "*") value = a * b;
        else if (operation === "/") value = type === "int" ? Math.trunc(a / b) : a / b;
        else if (operation === "%") value = a % b;
        return __cast(value, type, line);
    }
    function __format(value, type) {
        if (type !== "double") return String(value === 0 ? 0 : value);
        const sign = value < 0 || Object.is(value, -0) ? "-" : "";
        if (value === 0) return sign + "0";
        value = Math.abs(value);
        const data = new DataView(new ArrayBuffer(8));
        data.setFloat64(0, value);
        const bits = data.getBigUint64(0);
        const exponent = Number((bits >> 52n) & 2047n);
        const significand = (bits & ((1n << 52n) - 1n)) + (exponent ? 1n << 52n : 0n);
        const binaryPower = (exponent || 1) - 1023 - 52;
        let numerator = significand;
        let denominator = 1n;
        if (binaryPower >= 0) numerator <<= BigInt(binaryPower);
        else denominator <<= BigInt(-binaryPower);
        let power = Math.floor(Math.log10(value));
        const lessThanPower = power => power >= 0
            ? numerator < denominator * 10n ** BigInt(power)
            : numerator * 10n ** BigInt(-power) < denominator;
        while (lessThanPower(power)) --power;
        while (!lessThanPower(power + 1)) ++power;
        const scale = 5 - power;
        const scaledNumerator = scale >= 0 ? numerator * 10n ** BigInt(scale) : numerator;
        const scaledDenominator = scale < 0 ? denominator * 10n ** BigInt(-scale) : denominator;
        let rounded = scaledNumerator / scaledDenominator;
        const remainder = scaledNumerator % scaledDenominator;
        if (2n * remainder > scaledDenominator || (2n * remainder === scaledDenominator && rounded % 2n !== 0n)) ++rounded;
        if (rounded >= 1000000n) {
            rounded /= 10n;
            ++power;
        }
        const digits = rounded.toString().padStart(6, "0");
        if (power < -4 || power >= 6) {
            const mantissa = (digits[0] + "." + digits.slice(1)).replace(/\.?0+$/, "");
            const exponent = (power >= 0 ? "+" : "-") + String(Math.abs(power)).padStart(2, "0");
            return sign + mantissa + "e" + exponent;
        }
        const text = power >= 0
            ? digits.slice(0, power + 1) + "." + digits.slice(power + 1)
            : "0." + "0".repeat(-power - 1) + digits;
        return sign + text.replace(/\.?0+$/, "").replace(/\.$/, "");
    }
    function __write(text, line) {
        __output += text;
        __outputSize += new TextEncoder().encode(text).length;
        if (__outputSize > 65536) __fail("Output limit exceeded", line);
    }
)JS";
    Node* main = nullptr;
    for (Node* function : root->children) {
        if (function->kind != "Function") continue;
        if (function->text == "main") main = function;
        result << "    function f" << function->symbol << '(';
        auto& parameters = function->children[0]->children;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (index) result << ", ";
            result << variableName(parameters[index]);
        }
        result << ") {\n        __enter(" << function->line << ");\n        try {\n";
        for (Node* parameter : parameters)
            if (!parameter->array) result << "            " << variableName(parameter) << " = " << cast(variableName(parameter), parameter->type, parameter->line) << ";\n";
        result << "            __tick(" << function->children[1]->line << ");\n";
        for (Node* child : function->children[1]->children) statementCode(child, result, 3);
        if (function->text == "main") result << "            return 0;\n";
        else if (function->type == Type::Void) result << "            return;\n";
        else result << "            __fail(" << quote("Function '" + function->text + "' ended without returning a value") << ", " << function->line << ");\n";
        result << "        } finally {\n            __leave();\n        }\n    }\n";
    }
    result << "    try {\n        const exitCode = f" << main->symbol << "();\n";
    result << R"JS(        return {ok: true, output: __output, exitCode, steps: __steps};
    } catch (error) {
        return {ok: false, output: __output, error: {stage: "Runtime", message: error.message, line: error.line || 1}, steps: __steps};
    }
}

if (typeof module !== "undefined" && module.exports) {
    module.exports = runMiniCpp;
    if (require.main === module) {
        const result = runMiniCpp(__needsInput ? require("fs").readFileSync(0, "utf8") : "");
        process.stdout.write(result.output);
        if (result.ok) process.exitCode = result.exitCode & 255;
        else {
            process.stderr.write("Runtime error at line " + result.error.line + ": " + result.error.message + "\n");
            process.exitCode = 1;
        }
    }
}
)JS";
    return result.str();
}
