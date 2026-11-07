#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

enum class Type { Unknown, Int, Double, Bool, Void };

struct Node {
    int id;
    std::string kind;
    std::string text;
    int line;
    int column;
    Type type = Type::Unknown;
    int symbol = -1;
    bool grouped = false;
    bool array = false;
    int arraySize = 0;
    std::vector<Node*> children;
};

struct Diagnostic : std::runtime_error {
    std::string stage;
    int line;
    int column;
    Diagnostic(std::string stage, int line, int column, const std::string& message);
};

struct Context {
    std::vector<std::unique_ptr<Node>> nodes;
    Node* root = nullptr;
    Node* make(const std::string& kind, const std::string& text, int line, int column,
               std::initializer_list<Node*> children = {});
    Node* declaration(const std::string& type, Node* list);
    Node* function(const std::string& type, const std::string& name, Node* parameters,
                   Node* body, int line, int column);
};

struct Function {
    Type type;
    std::vector<Type> parameters;
    std::vector<bool> parameterArrays;
    int symbol;
    Node* definition = nullptr;
};

struct Analysis {
    std::map<std::string, Function> functions;
    std::vector<std::map<std::string, Node*>> scopes;
    int nextSymbol = 0;
    int loopDepth = 0;
    Type returnType = Type::Unknown;
    bool hasIostream = false;
    bool usesNamespace = false;
    void check(Node* root);
    void statement(Node* node);
    void scopedStatement(Node* node);
    Type expression(Node* node);
    Type scalar(Node* node);
    Type lvalue(Node* node);
    Node* variable(Node* node);
};

struct Value {
    Type type = Type::Void;
    double number = 0;
    bool initialized = true;
    std::shared_ptr<std::vector<Value>> elements = nullptr;
};

struct Flow {
    std::string kind;
    Value value;
};

struct Interpreter {
    Analysis& analysis;
    std::vector<std::map<int, Value>> frames;
    std::vector<std::map<int, int>> allocations;
    int arrayElements = 0;
    std::istringstream input;
    std::string output;
    int steps = 0;
    explicit Interpreter(Analysis& analysis, const std::string& inputText = "");
    void tick(Node* node);
    Value expression(Node* node);
    Value& target(Node* node);
    Value readInput(Node* node);
    Flow statement(Node* node);
    Value call(const Function& function, const std::vector<Value>& arguments, Node* site);
    Value run(Node* root);
};

std::string typeName(Type type);
Type parseType(const std::string& name);
std::string quote(const std::string& text);
std::string decodeString(const std::string& text, int line, int column);
std::string astJson(Node* node);
std::string generateJavaScript(Node* root);
std::string formatValue(Value value);
Value convert(Value value, Type type, Node* node);
Value calculate(const std::string& operation, Value left, Value right, Node* node);
