import json
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "minicpp"
HEADER = "#include <iostream>\n"


def analyze(source, input_text=""):
    result = subprocess.run([str(BINARY), "analyze", "-", "--input", input_text], input=source, text=True, capture_output=True, timeout=10)
    if not result.stdout:
        raise AssertionError(result.stderr or f"Process exited with {result.returncode}")
    return json.loads(result.stdout)


def run_javascript(code, directory, input_text=""):
    path = directory / "program.js"
    path.write_text(code, encoding="utf-8")
    script = "const run = require(process.argv[1]); process.stdout.write(JSON.stringify(run(process.argv[2] || '')));"
    process = subprocess.run(["node", "-e", script, str(path), input_text], capture_output=True, text=True, timeout=10)
    if process.returncode:
        raise AssertionError(process.stderr)
    return json.loads(process.stdout)


def run_cpp(source, directory, input_text=""):
    path = directory / "program.cpp"
    target = directory / "native"
    path.write_text(source, encoding="utf-8")
    process = subprocess.run(["g++", "-std=c++17", "-pedantic-errors", str(path), "-o", str(target)], capture_output=True, text=True, timeout=20)
    if process.returncode:
        raise AssertionError(process.stderr)
    process = subprocess.run([str(target)], input=input_text, capture_output=True, text=True, timeout=5)
    return process.stdout, process.returncode


class ProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("node"):
            raise RuntimeError("Node.js is required for JavaScript verification")

    def assert_valid(self, source, input_text=""):
        result = analyze(source, input_text)
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["execution"]["ok"], result["execution"])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            javascript = run_javascript(result["javascript"], directory, input_text)
            self.assertTrue(javascript["ok"], javascript)
            self.assertEqual(result["execution"]["output"], javascript["output"])
            self.assertEqual(result["execution"]["exitCode"], javascript["exitCode"])
            self.assertEqual(result["execution"]["steps"], javascript["steps"])
            output, exit_code = run_cpp(source, directory, input_text)
            self.assertEqual(output, javascript["output"])
            self.assertEqual(exit_code, javascript["exitCode"] & 255)
        return result

    def test_examples(self):
        expected = ["15\n", "Integer division = 2\nReal division = 2.5\nComparison = 1\nCast = 2\n", "120\n", "3 2 1 \n12\n"]
        for index, output in enumerate(expected, 1):
            path = next((ROOT / "examples").glob(f"{index:02}_*.cpp"))
            with self.subTest(example=path.name):
                result = self.assert_valid(path.read_text(encoding="utf-8"))
                self.assertEqual(result["execution"]["output"], output)

    def test_language_features(self):
        programs = {
            "precedence": 'int main(){ std::cout << 2 + 3 * 4 << "\\n"; return 0; }',
            "associativity": "int main(){ std::cout << 20 - 5 - 3 << \" \" << 20 / 2 / 2; return 0; }",
            "scope": "int main(){ int x=2; { double x=3.5; std::cout << x; } std::cout << x; return 0; }",
            "conversions": "int main(){ int x=-2.9; bool b=-2; double y=true; x+=2.5; std::cout << x << b << y << (double)5 / 2; return 0; }",
            "void": "void show(int n){ std::cout << n; return; } int main(){ show(7); return 0; }",
            "prototype": "double half(double n); int main(){ std::cout << half(5); return 0; } double half(double n){ return n/2; }",
            "parameters": "int sum(int a, int b){ return a+b; } int main(){ std::cout << sum(2.9,true); return 0; }",
            "namespace": "using namespace std; int main(){ cout << 3 << endl; return 0; }",
            "short_circuit": "int main(){ bool a=false && (1/0); bool b=true || (1/0); std::cout << a << b; return 0; }",
            "dangling_else": "int main(){ int x=0; if(true) if(false) x=1; else x=2; std::cout << x; return 0; }",
            "empty_for": "int main(){ int i=0; for(;;){ ++i; if(i==3) break; } std::cout << i; return 0; }",
            "nested_loops": "int main(){ int n=0; for(int i=0;i<3;i++){ for(int j=0;j<4;j++){ if(j==1)continue; if(j==3)break; n++; } } std::cout << n; return 0; }",
            "multiple_declarations": "int main(){ int a=2,b=a+3; b*=2; b/=3; b%=3; b-=1; std::cout << a << b; return 0; }",
            "booleans": "int main(){ bool a=true; a+=2; std::cout << (a && !false) << (2<=3) << (4>=4) << (1!=2) << (2==2); return 0; }",
            "negative_remainder": "int main(){ std::cout << -7/3 << \" \" << -7%3 << \" \" << 7%-3; return 0; }",
            "minimum_integer": "int main(){ int x=-2147483648; std::cout << x << \" \" << (int)2147483647.9; return 0; }",
            "floating_format": "int main(){ std::cout << 0.0 << \" \" << -0.0 << \" \" << 1e-7 << \" \" << 1e7 << \" \" << 1.23456789 << \" \" << 999999.9 << \" \" << 0.00009999999 << \" \" << 123456.5 << \" \" << 123457.5 << \" \" << 1e-300 << \" \" << 1e300; return 0; }",
            "comments": "int main(){ /* block\ncomment */ int x=2; // line\n std::cout << x; return 0; }",
            "no_explicit_return": "int main(){ std::cout << 5; }",
            "nonzero_exit": "int main(){ return -3; }",
            "nested_scope_in_for": "int main(){ for(int i=0;i<1;i++){ {int i=8; std::cout << i;} } return 0; }",
            "escaped_strings": 'int main(){ std::cout << "a\\t\\\"b\\\\c\\n"; return 0; }',
        }
        for name, program in programs.items():
            with self.subTest(feature=name):
                self.assert_valid(HEADER + program)

    def test_generated_arithmetic(self):
        generator = random.Random(42)
        expressions = []
        for unused in range(40):
            a, b, c = [generator.randint(-30, 30) for unused in range(3)]
            expressions.append(f"(({a}) + ({b}) * ({c}))")
        program = "int main(){" + "".join(f'std::cout << {expression} << " ";' for expression in expressions) + "return 0;}"
        self.assert_valid(HEADER + program)

    def test_arrays(self):
        programs = {
            "partial_initialization": 'int main(){int a[4]={1,2};std::cout << a[0] << a[1] << a[2] << a[3];return 0;}',
            "inferred_size": 'int main(){int a[]={3,1,4,};std::cout << a[2];return 0;}',
            "types": 'int main(){bool b[]={false,true};double a[]={1,2.5};b[0]+=2;a[0]/=2;std::cout << b[0] << b[1] << a[0] << a[1];return 0;}',
            "constant_initializers": 'int main(){bool b[]={1-1,1};double a[]={2+3,4};std::cout << b[0] << b[1] << a[0];return 0;}',
            "parameter_alias": 'void fill(int a[],int n){for(int i=0;i<n;i++)a[i]+=10;}int main(){int a[3]={1,2};fill(a,3);std::cout << a[0] << a[1] << a[2];return 0;}',
            "prototype": 'int first(int a[]);int main(){int a[]={7};std::cout << first(a);return 0;}int first(int a[]){return a[0];}',
            "shadowing": 'int main(){int a[]={1};{double a[]={2.5};std::cout << a[0];}std::cout << a[0];return 0;}',
            "single_index_evaluation": 'int next(int cursor[]){int old=cursor[0];cursor[0]++;return old;}int main(){int cursor[]={0};int a[]={8,12,0};a[next(cursor)]/=2;std::cout << a[0] << " " << a[1] << " " << cursor[0];return 0;}',
            "rhs_before_lvalue": 'int change(int a[]){a[0]=10;return 2;}int main(){int a[]={3};a[0]+=change(a);std::cout << a[0];return 0;}',
            "recursive_alias": 'void fill(int a[],int n){if(n==0)return;a[n-1]=n;fill(a,n-1);}int main(){int a[3];fill(a,3);std::cout << a[0] << a[1] << a[2];return 0;}',
            "updates": 'int main(){int a[]={2};a[0]++;--a[0];a[0]*=3;a[0]%=4;std::cout << a[0];return 0;}',
            "allocation_reuse": 'int main(){for(int i=0;i<20;i++){int a[4096]={};a[0]=i;}return 0;}',
        }
        for name, program in programs.items():
            with self.subTest(feature=name):
                self.assert_valid(HEADER + program)
        path = ROOT / "examples" / "08_array_input.cpp"
        result = self.assert_valid(path.read_text(encoding="utf-8"), "5 4 1 5 2 3")
        self.assertEqual(result["execution"]["output"], "Sum = 15\nSorted = 1 2 3 4 5 \n")

    def test_do_while_and_conditional(self):
        programs = [
            'int main(){int i=0;do{i++;}while(false);std::cout << i;return 0;}',
            'int main(){int i=0;do{i++;if(i==2)continue;std::cout << i;}while(i<4);return 0;}',
            'int main(){int i=0;do{i++;if(i==2)break;}while(true);std::cout << i;return 0;}',
            'int f(){do{return 8;}while(true);}int main(){std::cout << f();return 0;}',
            'int main(){std::cout << (true?7:1/0) << (false?1/0:9);return 0;}',
            'int main(){std::cout << (true?5:2.5)/2 << (false?false:3);return 0;}',
            'int main(){std::cout << (false?1:true?2:3) << (true?false?1:2:3);return 0;}',
        ]
        for program in programs:
            with self.subTest(program=program):
                self.assert_valid(HEADER + program)
        path = ROOT / "examples" / "09_do_while_conditional.cpp"
        result = self.assert_valid(path.read_text(encoding="utf-8"))
        self.assertEqual(result["execution"]["output"], "Maximum = 4\n")

    def test_input(self):
        programs = [
            ('int main(){int n;double d;bool b;std::cin >> n >> d >> b;std::cout << n*2 << " " << d/2 << " " << b;return 0;}', "21 2.5 1"),
            ('using namespace std;int main(){double a[2];cin >> a[0] >> a[1];cout << a[0]+a[1];return 0;}', "-1.25\n+.5"),
            ('int main(){int n;double d;bool b;std::cin >> n >> d >> b;std::cout << n << " " << d << b;return 0;}', "-2147483648 1e-300 0"),
        ]
        for program, input_text in programs:
            with self.subTest(program=program):
                self.assert_valid(HEADER + program, input_text)

    def test_input_errors(self):
        programs = [
            ("int main(){int n;std::cin >> n;return 0;}", "", "Input exhausted"),
            ("int main(){int n;std::cin >> n;return 0;}", "2.5", "Invalid numeric input"),
            ("int main(){int n;std::cin >> n;return 0;}", "2147483648", "Invalid numeric input"),
            ("int main(){double n;std::cin >> n;return 0;}", "1e999", "Invalid numeric input"),
            ("int main(){bool n;std::cin >> n;return 0;}", "2", "Invalid numeric input"),
            ("int main(){int a[2];std::cin >> a[2];return 0;}", "5", "Array index out of bounds"),
        ]
        for program, input_text, message in programs:
            with self.subTest(program=program):
                result = analyze(HEADER + program, input_text)
                self.assertTrue(result["ok"], result)
                with tempfile.TemporaryDirectory() as temporary:
                    javascript = run_javascript(result["javascript"], Path(temporary), input_text)
                for execution in (result["execution"], javascript):
                    self.assertFalse(execution["ok"], execution)
                    self.assertEqual(execution["error"]["stage"], "Runtime")
                    self.assertIn(message, execution["error"]["message"])

    def test_ast_precedence(self):
        result = analyze("int main(){ return 2+3*4; }")
        returned = result["ast"]["children"][0]["children"][1]["children"][0]["children"][0]
        self.assertEqual(returned["text"], "+")
        self.assertEqual(returned["children"][1]["text"], "*")

    def test_frontend_errors(self):
        programs = [
            ("int main(){int x=;return 0;}", "Syntax", "unexpected"),
            ("int main(){@return 0;}", "Lexical", "Unexpected character"),
            ("int main(){ /* never closed", "Lexical", "Unterminated comment"),
            ('int main(){std::cout << "never closed;}', "Lexical", "Unterminated string"),
            ("int main(){x=2;return 0;}", "Semantic", "Undeclared variable"),
            ("int main(){int x; int x;return 0;}", "Semantic", "Redeclaration"),
            ("int main(){break;return 0;}", "Semantic", "inside a loop"),
            ("int main(){continue;return 0;}", "Semantic", "inside a loop"),
            ("int main(){double x=5.0%2;return 0;}", "Semantic", "integral operands"),
            ("void f(){return;} int main(){int x=f();return 0;}", "Semantic", "value is required"),
            ("int f(int x){return x;} int main(){return f();}", "Semantic", "number of arguments"),
            ("int main(){return f();} int f(){return 1;}", "Semantic", "Undeclared function"),
            ("int main(){int f=1;return f();}", "Semantic", "not callable"),
            ("int f(){return 0;}", "Semantic", "int main"),
            ("void main(){return;}", "Semantic", "main must have type int"),
            ("int main(){bool b=true;b++;return 0;}", "Semantic", "bool"),
            ("int main(){std::cout << 1 < 2;return 0;}", "Semantic", "Parenthesize"),
            ("int main(){for(int i=0;i<1;i++){int i=2;}return 0;}", "Semantic", "Redeclaration"),
            ("int main(){for(int i=0;i<1;i++){}return i;}", "Semantic", "Undeclared variable"),
            ("int main(){return 2147483648;}", "Semantic", "32-bit range"),
            ("int f(); int main(){return 0;}", "Semantic", "has no definition"),
            ("int main(){return " + "+ " * 300 + "1;}", "Input", "AST depth limit"),
            (" " * 32769, "Input", "32768 bytes"),
            ("int main(){int a[0];return 0;}", "Semantic", "Array size"),
            ("int main(){int a[4097];return 0;}", "Semantic", "Array size"),
            ("int main(){int a[]={};return 0;}", "Semantic", "Array size"),
            ("int main(){int a[1]={1,2};return 0;}", "Semantic", "Too many"),
            ("int main(){int a[2];return a[0.5];}", "Semantic", "index must be integral"),
            ("int main(){int a=2;return a[0];}", "Semantic", "not an array"),
            ("int main(){int a[2];a=3;return 0;}", "Semantic", "as a whole"),
            ("int main(){int a[2];return a+1;}", "Semantic", "scalar value"),
            ("int f(int a[]){return a[0];}int main(){return f(3);}", "Semantic", "Array argument"),
            ("int f(int a[]){return a[0];}int main(){double a[2];return f(a);}", "Semantic", "Array argument"),
            ("int f(int a){return a;}int main(){int a[2];return f(a);}", "Semantic", "scalar value"),
            ("int main(){int a[2][3];return 0;}", "Syntax", "unexpected"),
            ("int main(){int a[2][3][4];return 0;}", "Syntax", "unexpected"),
            ("int main(){int a[1]={2.5};return 0;}", "Semantic", "Narrowing"),
            ("int main(){bool a[1]={2};return 0;}", "Semantic", "Narrowing"),
            ("int main(){int n=2;double a[]={n};return 0;}", "Semantic", "Narrowing"),
        ]
        for source, stage, message in programs:
            with self.subTest(source=source):
                result = analyze(HEADER + source)
                self.assertFalse(result["ok"])
                self.assertEqual(result["error"]["stage"], stage)
                self.assertIn(message, result["error"]["message"])

    def test_runtime_errors(self):
        programs = [
            ("int main(){return 1/0;}", "Division by zero"),
            ("int main(){int x;return x;}", "uninitialized variable"),
            ("int main(){int x=2147483647;return x+1;}", "Integer overflow"),
            ("int main(){return -2147483648/-1;}", "Integer overflow"),
            ("int main(){return -2147483648%-1;}", "Integer overflow"),
            ("int main(){return (int)1e50;}", "Integer overflow"),
            ("int main(){double x=1e308*1e308;return 0;}", "Non-finite"),
            ("int main(){while(true){}return 0;}", "step limit"),
            ("int f(){return f();}int main(){return f();}", "Call depth"),
            ("int f(){}int main(){return f();}", "without returning"),
            ("int main(){for(int i=0;i<2;i++){int x;if(i==0)x=1;std::cout << x;}return 0;}", "uninitialized"),
            ('int main(){for(int i=0;i<40;i++)std::cout << "' + "a" * 2000 + '";return 0;}', "Output limit"),
            ("int main(){int a[2]={};return a[-1];}", "Array index out of bounds"),
            ("int main(){int a[2]={};a[2]=1;return 0;}", "Array index out of bounds"),
            ("int main(){int a[2];return a[0];}", "uninitialized"),
            ("int main(){int a[2]={a[1],2};return 0;}", "uninitialized"),
            ("int main(){" + "".join(f"int a{index}[4096];" for index in range(17)) + "return 0;}", "Array storage limit"),
        ]
        for source, message in programs:
            with self.subTest(source=source):
                result = analyze(HEADER + source)
                self.assertTrue(result["ok"], result)
                execution = result["execution"]
                self.assertFalse(execution["ok"], execution)
                self.assertIn(message, execution["error"]["message"])
                with tempfile.TemporaryDirectory() as temporary:
                    javascript = run_javascript(result["javascript"], Path(temporary))
                self.assertFalse(javascript["ok"], javascript)
                self.assertIn(message, javascript["error"]["message"])
                self.assertEqual(execution["output"], javascript["output"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
