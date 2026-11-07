#include "core.hpp"
#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>

struct yy_buffer_state;
yy_buffer_state* yy_scan_bytes(const char* bytes, int length);
void yy_delete_buffer(yy_buffer_state* buffer);
int yylex_destroy();

static std::string errorJson(const Diagnostic& error) {
    return "{\"stage\":" + quote(error.stage) + ",\"message\":" + quote(error.what()) +
        ",\"line\":" + std::to_string(error.line) + ",\"column\":" + std::to_string(error.column) + "}";
}

int main(int argc, char** argv) {
    std::locale::global(std::locale::classic());
    if (argc != 3 && (argc != 5 || std::string(argv[3]) != "--input")) {
        std::cerr << "Usage: minicpp check|run|ast|compile|analyze file.cpp\n";
        std::cerr << "Use - to read source from standard input.\n";
        std::cerr << "Optional runtime input: --input \"5 2.5\"\n";
        return 2;
    }
    std::string mode = argv[1];
    if (mode != "check" && mode != "run" && mode != "ast" && mode != "compile" && mode != "analyze") {
        std::cerr << "Unknown command\n";
        return 2;
    }
    yy_buffer_state* buffer = nullptr;
    try {
        std::string inputText = argc == 5 ? argv[4] : "";
        if (inputText.size() > 32768) throw Diagnostic("Input", 1, 1, "Runtime input limit is 32768 bytes");
        std::string source;
        if (std::string(argv[2]) == "-") source.assign(std::istreambuf_iterator<char>(std::cin), {});
        else {
            std::ifstream file(argv[2], std::ios::binary);
            if (!file) throw Diagnostic("Input", 1, 1, "Cannot open source file");
            source.assign(std::istreambuf_iterator<char>(file), {});
        }
        if (source.size() > 32768) throw Diagnostic("Input", 1, 1, "Source limit is 32768 bytes");
        if (source.find('\0') != std::string::npos) throw Diagnostic("Lexical", 1, 1, "NUL bytes are not allowed");
        Context context;
        buffer = yy_scan_bytes(source.data(), static_cast<int>(source.size()));
        yy::parser parser(context);
        parser.parse();
        yy_delete_buffer(buffer);
        buffer = nullptr;
        yylex_destroy();
        std::vector<std::pair<Node*, int>> pending = {{context.root, 0}};
        while (!pending.empty()) {
            auto current = pending.back();
            pending.pop_back();
            if (current.second > 256)
                throw Diagnostic("Input", current.first->line, current.first->column, "AST depth limit exceeded");
            for (Node* child : current.first->children) pending.push_back({child, current.second + 1});
        }
        Analysis analysis;
        analysis.check(context.root);
        if (mode == "check") { std::cout << "Syntax and semantic checks passed\n"; return 0; }
        if (mode == "ast") { std::cout << astJson(context.root) << '\n'; return 0; }
        std::string code = generateJavaScript(context.root);
        if (mode == "compile") { std::cout << code; return 0; }
        Interpreter interpreter(analysis, inputText);
        if (mode == "run") {
            try {
                Value exitCode = interpreter.run(context.root);
                std::cout << interpreter.output;
                return static_cast<int>(exitCode.number) & 255;
            }
            catch (...) { std::cout << interpreter.output; throw; }
        }
        std::ostringstream result;
        result << "{\"ok\":true,\"ast\":" << astJson(context.root) << ",\"javascript\":" << quote(code) << ",\"execution\":";
        try {
            Value exitCode = interpreter.run(context.root);
            result << "{\"ok\":true,\"output\":" << quote(interpreter.output)
                      << ",\"exitCode\":" << static_cast<int>(exitCode.number) << ",\"steps\":" << interpreter.steps << "}}\n";
        }
        catch (const Diagnostic& error) {
            result << "{\"ok\":false,\"output\":" << quote(interpreter.output)
                      << ",\"error\":" << errorJson(error) << ",\"steps\":" << interpreter.steps << "}}\n";
        }
        std::cout << result.str();
        return 0;
    }
    catch (const Diagnostic& error) {
        if (buffer) { yy_delete_buffer(buffer); yylex_destroy(); }
        if (mode == "analyze") std::cout << "{\"ok\":false,\"error\":" << errorJson(error) << "}\n";
        else std::cerr << error.stage << " error at " << error.line << ':' << error.column << ": " << error.what() << '\n';
        return 1;
    }
    catch (const std::exception& error) {
        if (buffer) { yy_delete_buffer(buffer); yylex_destroy(); }
        Diagnostic diagnostic("Internal", 1, 1, error.what());
        if (mode == "analyze") std::cout << "{\"ok\":false,\"error\":" << errorJson(diagnostic) << "}\n";
        else std::cerr << "Internal error: " << error.what() << '\n';
        return 1;
    }
}
