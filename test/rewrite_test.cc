#include <functional>
#include <memory>
#include <optional>
#include <trieste/trieste.h>

using namespace trieste;

bool tests_failed = false;

inline const auto A = TokenDef("A");
inline const auto B = TokenDef("B");
inline const auto C = TokenDef("C");
inline const auto D = TokenDef("D");
inline const auto E = TokenDef("E");
inline const auto F = TokenDef("F");
inline const auto G = TokenDef("G");
inline const auto H = TokenDef("H");

inline const auto Symbol = TokenDef("Symbol", flag::symtab);

inline const auto Block = TokenDef("Block", flag::symtab);

const auto test_wf = (Group <<= (A | B | C | D | E | F | G | H | Symbol)++) |
  (Top <<= File) | (File <<= Group);

Parse test_parser()
{
  Parse p(depth::file, test_wf);

  p("start", // this indicates the 'mode' these rules are associated with
    {// Whitespace between tokens.
     "[[:blank:]]+" >> [](auto&) {}, // no-op

     "A" >> [](auto& m) { m.push(A); m.pop(A); },

     "B" >> [](auto& m) { m.add(B); },

     "C" >> [](auto& m) { m.add(C); },

     "D" >> [](auto& m) { m.add(D); },

     "E" >> [](auto& m) { m.add(E); },

     "F" >> [](auto& m) { m.add(F); },

     "G" >> [](auto& m) { m.add(G); },

     "H" >> [](auto& m) { m.add(H); },

     "Symbol" >> [](auto& m) { m.add(Symbol); },

     "\\(" >> [](auto& m) { m.push(Group); },

     "\\)" >>
       [](auto& m) {
         // Terminate the current group
         m.term();
       },

     "{" >> [](auto& m) { m.push(Block); },

     "}" >>
       [](auto& m) {
         m.term();
         m.pop(Block);
       }});

  return p;
};

void run_test(
  std::string test_name,
  std::vector<std::pair<std::string, std::string>> testCases,
  PassDef pass_def)
{
  Parse parser = test_parser();
  Pass pass = pass_def;

  int count = 0;
  for (auto& [input, expected] : testCases)
  {
    count++;
    auto n = parser.parse(SourceDef::synthetic(input));
    auto e = parser.parse(SourceDef::synthetic(expected));

    auto [r, iterations, changes] = pass->run(n);

    if (n->equals(e))
    {
      std::cout << test_name << " (" << count << ") - passed" << std::endl;
    }
    else
    {
      std::cout << test_name << " (" << count << ") - failed" << std::endl;

      // Re-parse as pass updates in place.
      n = parser.parse(SourceDef::synthetic(input));

      std::cout << "----------------Input--------------" << std::endl
                << n << std::endl;

      std::cout << "----------------Output-------------" << std::endl
                << r << std::endl;

      std::cout << "----------------Expected-----------" << std::endl
                << e << std::endl;
      tests_failed = true;
    }
  }
}

int main()
{
  // This provides simple tests of the implicitly inserted group tokens, which
  // can be explicit with this parser using `(` and `)`.
  run_test(
    "Identity test",
    {{"A", "A"}, {"(A)", "A"}, {"A", "(A)"}, {"{A}", "{(A)}"}},
    PassDef("start", test_wf));

  // This provides a simple test of removing a group.
  auto p1 = PassDef(
    "Single group drop",
    test_wf,
    {In(Group) * T(Group) << Any[A] * End >> [](Match& _) { return _(A); }});
  run_test(
    "Group test", {{"(A)", "A"}, {"((A))", "A"}, {"((A B))", "((A B))"}}, p1);


  // This provides a simple test of lifting a group.
  auto p2 = PassDef(
    "Lift order issue",
    test_wf,
    {In(Group) * T(A) >> [](Match&) { return Lift << Block << C; },
     T(B) * T(D) >> [](Match&) { return Seq << E << F; }});

  run_test("Lift test", {{"{(A A)}", "{C C}"}, {"{(B A D)}", "{C (E F)}"}}, p2);

  if (tests_failed)
    return 1;
  return 0;
}