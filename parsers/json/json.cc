#include "json.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  std::size_t
  invalid_tokens(Node n, const std::map<Token, std::string>& token_messages)
  {
    std::size_t changes = 0;
    for (auto child : *n)
    {
      if (token_messages.count(child->type()) > 0)
      {
        n->replace(child, err(child, token_messages.at(child->type())));
        changes += 1;
      }
      else
      {
        changes += invalid_tokens(child, token_messages);
      }
    }

    return changes;
  }
}

namespace trieste::json
{

  const auto ValueToken = T(Object, Array, String, Number, True, False, Null);

  PassDef groups()
  {
    PassDef groups = {
      "groups",
      wf_groups,
      dir::bottomup | dir::once,
      {
        (T(Group) << (Any[Group] * End)) >>
          [](Match& _) { return _(Group); },

        In(Top) * (T(File) << (ValueToken[Value] * End)) >>
          [](Match& _) { return _(Value); },

        In(Array) * (T(Comma) << ((T(Group) << End) * ValueToken++[Value] * End)) >>
          [](Match& _) { return Seq << _[Value]; },

        In(Object) * (T(Comma) << ((T(Group) << End) * T(Member)++[Member] * End)) >>
          [](Match& _) { return Seq << _[Member]; },

        In(Top) * T(File)[File] >>
          [](Match& _) { return err(_[File], "Invalid JSON"); },

        In(Array) * T(Comma)[Comma] >>
          [](Match& _) { return err(_[Comma], "Cannot parse array body!"); },

        In(Object) * (T(Comma)[Comma]) >>
          [](Match& _) { return err(_[Member], "Cannot parse object body!"); },

        (T(Member)[Member] << --(T(String) * ValueToken * End)) >>
          [](Match& _) { return err(_[Member], "Invalid member!"); },

        In(Object) * (!T(Member))[Member] >> 
          [](Match& _) { return err(_[Member], "Invalid member!"); },

        In(Array) * (!ValueToken)[Value] >> 
          [](Match& _) { return err(_[Value], "Invalid value in array!"); },
      }};

    groups.post([&](Node n) {
      return invalid_tokens(
        n, {{Comma, "Invalid parsing"}, {trieste::Invalid, "Unable to parse here!"}, {Group, "Invalid parsing"}});
    });

    return groups;
  }

  std::vector<Pass> passes()
  {
    return {groups()};
  }
}
