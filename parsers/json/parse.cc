#include "json.h"

namespace trieste::json
{
  Parse parser()
  {
    Parse p(depth::file, wf_parse);
    auto depth = std::make_shared<int>(0);

    p("start",
      {"[ \r\n\t]+" >> [](auto&) { return; },

       ":" >> [](auto& m) { m.seq(Member); },

       "," >> [](auto& m) {
          m.seq(Comma, {Member});
          //  Push a group as ',' is a separator, and not a terminator
          //  An empty group at the end can be used to detect trailing commas
          m.push(Group);
        },

       "{" >>
         [depth](auto& m) {
           if ((*depth)++ > 500)
           {
             // TODO: Remove this once Trieste can handle deeper stacks
             m.error("Too many nested objects");
             return;
           }
           m.push(Object);
           // Begin sequence to not have a nested group, this means we will need to match
           // this empty first element of Comma, but gives a better WF definition.
           m.seq(Comma);
         },

       "}" >>
         [depth](auto& m) {
          (*depth)--;
           m.term({Member, Comma});
           m.pop(Object, "Unexpected '}'!");
         },

       R"(\[)" >>
         [depth](auto& m) {
           if ((*depth)++ > 500)
           {
             // TODO: Remove this once Trieste can handle deeper stacks
             m.error("Too many nested objects");
             return;
           }
           m.push(Array);
           m.seq(Comma);
         },

       "]" >>
         [depth](auto& m) {
          (*depth)--;
           m.term({Comma});
           m.pop(Array, "Unexpected ']'!");
         },

       "true" >> [](auto& m) { m.add(True); },

       "false" >> [](auto& m) { m.add(False); },

       "null" >> [](auto& m) { m.add(Null); },

       // RE for a JSON number:
       // -? : optional minus sign
       // (?:0|[1-9][0-9]*) : either a single 0, or 1-9 followed by any digits
       // (?:\.[0-9]+)? : optionally, a single period followed by one or more digits (fraction)
       // (?:[eE][-+]?[0-9]+)? : optionally, an exponent. This can start with e or E,
       //                        have +/-/nothing, and then 1 or more digits
       R"(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)" >>
         [](auto& m) { m.add(Number); },

       // RE for a JSON string:
       // " : a double quote followed by either:
       // 1. [^"\\\x00-\x1F]+ : one or more characters that are not a double quote, backslash,
       //                       or a control character from 00-1f
       // 2. \\["\\\/bfnrt] : a backslash followed by one of the characters ", \, /, b, f, n, r, or t
       // 3. \\u[[:xdigit:]]{4} : a backslash followed by u, followed by 4 hex digits
       // zero or more times and then
       // " : a double quote
       R"("(?:[^"\\\x00-\x1F]+|\\["\\\/bfnrt]|\\u[[:xdigit:]]{4})*")" >>
         [](auto& m) { m.add(String); },

       "." >> [](auto& m) { m.error("Invalid character"); }});

    return p;
  }
}
