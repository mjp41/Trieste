// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
/// To get FAST PATH macros
#include "snmalloc/ds_core/defines.h"

#include <cassert>
#include <functional>
#include <optional>

namespace trieste
{
  class PassDef;

  class Match
  {
  private:
    Node in_node;
    std::map<Token, NodeRange> captures;

  public:
    Match(Node in_node) : in_node(in_node) {}

    Location fresh(const Location& prefix = {})
    {
      return in_node->fresh(prefix);
    }

    NodeRange& operator[](const Token& token)
    {
      return captures[token];
    }

    Node operator()(const Token& token)
    {
      auto it = captures.find(token);
      if ((it != captures.end()) && *it->second.first)
        return *it->second.first;

      return {};
    }

    void operator+=(const Match& that)
    {
      captures.insert(that.captures.begin(), that.captures.end());
    }
  };

  namespace detail
  {
    class PatternDef
    {
    public:
      virtual ~PatternDef() = default;

      virtual bool custom_rep()
      {
        return false;
      }

      virtual bool match(NodeIt&, NodeIt, Match&) const
      {
        return false;
      }
    };

    using PatternPtr = std::shared_ptr<PatternDef>;

    template <typename P>
    class Cap : public PatternDef
    {
    private:
      Token name;
      std::shared_ptr<P> pattern;

    public:
      Cap(const Token& name, std::shared_ptr<P> pattern) : name(name), pattern(pattern)
      {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto begin = it;
        auto match2 = match;

        if (!pattern->match(it, end, match2))
          return false;

        match += match2;
        match[name] = {begin, it};
        return true;
      }
    };

    class Anything : public PatternDef
    {
    public:
      Anything() {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if (it == end)
          return false;

        ++it;
        return true;
      }
    };

    class TokenMatch : public PatternDef
    {
    private:
      Token type;

    public:
      TokenMatch(const Token& type) : type(type) {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if ((it == end) || ((*it)->type() != type))
          return false;

        ++it;
        return true;
      }
    };

    class RegexMatch : public PatternDef
    {
    private:
      Token type;
      RE2 regex;

    public:
      RegexMatch(const Token& type, const std::string& r) : type(type), regex(r)
      {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if ((it == end) || ((*it)->type() != type))
          return false;

        if (!RE2::FullMatch((*it)->location().view(), regex))
          return false;

        ++it;
        return true;
      }
    };

    template <typename P>
    class Opt : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;

    public:
      Opt(std::shared_ptr<P> pattern) : pattern(pattern) {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto match2 = match;

        if (pattern->match(it, end, match2))
          match += match2;

        return true;
      }
    };

    template <typename P>
    class Rep : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;

    public:
      Rep(std::shared_ptr<P> pattern) : pattern(pattern) {}

      bool custom_rep() override
      {
        // Rep(Rep(...)) is treated as Rep(...).
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        if (pattern->custom_rep())
          return pattern->match(it, end, match);

        while ((it != end) && pattern->match(it, end, match))
          ;
        return true;
      }
    };

    template <typename P>
    class Not : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;

    public:
      Not(std::shared_ptr<P> pattern) : pattern(pattern) {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        if (it == end)
          return false;

        auto match2 = match;
        auto begin = it;

        if (pattern->match(it, end, match2))
        {
          it = begin;
          return false;
        }

        it = begin + 1;
        return true;
      }
    };

    template <typename P1, typename P2>
    class Seq : public PatternDef
    {
    private:
      std::shared_ptr<P1> first;
      std::shared_ptr<P2> second;

    public:
      Seq(std::shared_ptr<P1> first, std::shared_ptr<P2> second) : first(first), second(second) {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto match2 = match;
        auto begin = it;

        if (!first->match(it, end, match2))
          return false;

        if (!second->match(it, end, match2))
        {
          it = begin;
          return false;
        }

        match += match2;
        return true;
      }
    };

    template <typename P1, typename P2>
    class Choice : public PatternDef
    {
    private:
      std::shared_ptr<P1> first;
      std::shared_ptr<P2> second;

    public:
      Choice(std::shared_ptr<P1> first, std::shared_ptr<P2> second) : first(first), second(second)
      {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto match2 = match;

        if (first->match(it, end, match2))
        {
          match += match2;
          return true;
        }

        auto match3 = match;

        if (second->match(it, end, match3))
        {
          match += match3;
          return true;
        }

        return false;
      }
    };

    class Inside : public PatternDef
    {
    private:
      Token type;
      bool any;

    public:
      Inside(const Token& type) : type(type), any(false) {}

      bool custom_rep() override
      {
        // Rep(Inside) checks for any parent, not just the immediate parent.
        any = true;
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        while (p)
        {
          if (p->type() == type)
            return true;

          if (!any)
            break;

          p = p->parent();
        }

        return false;
      }
    };

    class InsideN : public PatternDef
    {
    private:
      std::vector<Token> types;
      bool any;

    public:
      InsideN(const std::vector<Token>& types) : types(types), any(false) {}

      bool custom_rep() override
      {
        // Rep(InsideN) checks for any parent, not just the immediate parent.
        any = true;
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();

        while (p)
        {
          if (p->type().in(types))
            return true;

          if (!any)
            break;

          p = p->parent();
        }

        return false;
      }
    };

    class First : public PatternDef
    {
    public:
      First() {}

      bool custom_rep() override
      {
        // Rep(First) is treated as First.
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        if (it == end)
          return false;

        auto p = (*it)->parent();
        return p && (it == p->begin());
      }
    };

    class Last : public PatternDef
    {
    public:
      Last() {}

      bool custom_rep() override
      {
        // Rep(Last) is treated as Last.
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match&) const override
      {
        return it == end;
      }
    };

    template <typename P, typename C>
    class Children : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;
      std::shared_ptr<C> children;

    public:
      Children(std::shared_ptr<P> pattern, std::shared_ptr<C> children)
      : pattern(pattern), children(children)
      {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto match2 = match;
        auto begin = it;

        if (!pattern->match(it, end, match2))
          return false;

        auto it2 = (*begin)->begin();
        auto end2 = (*begin)->end();

        if (!children->match(it2, end2, match2))
        {
          it = begin;
          return false;
        }

        match += match2;
        return true;
      }
    };

    template <typename P>
    class Pred : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;

    public:
      Pred(std::shared_ptr<P> pattern) : pattern(pattern) {}

      bool custom_rep() override
      {
        // Rep(Pred(...)) is treated as Pred(...).
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto begin = it;
        auto match2 = match;
        bool ok = pattern->match(it, end, match2);
        it = begin;
        return ok;
      }
    };

    template <typename P>
    class NegPred : public PatternDef
    {
    private:
      std::shared_ptr<P> pattern;

    public:
      NegPred(std::shared_ptr<P> pattern) : pattern(pattern) {}

      bool custom_rep() override
      {
        // Rep(NegPred(...)) is treated as NegPred(...).
        return true;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto begin = it;
        auto match2 = match;
        bool ok = pattern->match(it, end, match2);
        it = begin;
        return !ok;
      }
    };

    using ActionFn = std::function<bool(const NodeRange&)>;

    // TODO carry lambda term type here too.
    template <typename P>
    class Action : public PatternDef
    {
    private:
      ActionFn action;
      std::shared_ptr<P> pattern;

    public:
      Action(ActionFn action, std::shared_ptr<P> pattern)
      : action(action), pattern(pattern)
      {}

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const override
      {
        auto begin = it;
        auto match2 = match;

        if (!pattern->match(it, end, match2))
          return false;

        if (!action({begin, it}))
        {
          it = begin;
          return false;
        }

        match += match2;
        return true;
      }
    };

    template <typename P>
    class Pattern;

    template<typename T>
    using Effect = std::function<T(Match&)>;

    template<typename T>
    using PatternEffect = std::pair<PatternPtr, Effect<T>>;

    template <typename P>
    class Pattern
    {
      template <typename P1>
      friend class Pattern;
    private:
      std::shared_ptr<P> pattern;

    public:
      Pattern(std::shared_ptr<P> pattern) : pattern(pattern) {}

      operator PatternPtr() const
      {
        return pattern;
      }

      SNMALLOC_FAST_PATH bool match(NodeIt& it, NodeIt end, Match& match) const
      {
        return pattern->match(it, end, match);
      }

      Pattern<Action<P>> operator()(ActionFn action) const
      {
        return {std::make_shared<Action<P>>(action, pattern)};
      }

      Pattern<Cap<P>> operator[](const Token& name) const
      {
        return {std::make_shared<Cap<P>>(name, pattern)};
      }

      Pattern<Opt<P>> operator~() const
      {
        return {std::make_shared<Opt<P>>(pattern)};
      }

      Pattern<Pred<P>> operator++() const
      {
        return {std::make_shared<Pred<P>>(pattern)};
      }

      Pattern<NegPred<P>> operator--() const
      {
        return {std::make_shared<NegPred<P>>(pattern)};
      }

      Pattern<Rep<P>> operator++(int) const
      {
        return {std::make_shared<Rep<P>>(pattern)};
      }

      Pattern<Not<P>> operator!() const
      {
        return {std::make_shared<Not<P>>(pattern)};
      }

      template <typename P2>
      Pattern<Seq<P, P2>> operator*(Pattern<P2> rhs) const
      {
        return {std::make_shared<Seq<P,P2>>(pattern, rhs.pattern)};
      }

      template <typename P2>
      Pattern<Choice<P,P2>> operator/(Pattern<P2> rhs) const
      {
        return {std::make_shared<Choice<P,P2>>(pattern, rhs.pattern)};
      }

      template <typename P2>
      Pattern<Children<P,P2>> operator<<(Pattern<P2> rhs) const
      {
        return {std::make_shared<Children<P, P2>>(pattern, rhs.pattern)};
      }
    };

    struct RangeContents
    {
      NodeRange range;
    };

    struct RangeOr
    {
      NodeRange range;
      Node node;
    };

    struct EphemeralNode
    {
      Node node;
    };

    struct EphemeralNodeRange
    {
      NodeRange range;
    };
  }

  // TODO here we could lift the change further
  template<typename F, typename P>
  inline auto operator>>(detail::Pattern<P> pattern, F effect)
    -> detail::PatternEffect<decltype(effect(std::declval<Match&>()))>
  {
    return {(detail::PatternPtr)pattern, effect};
  }

  inline const auto Any = detail::Pattern<detail::Anything>(std::make_shared<detail::Anything>());
  inline const auto Start = detail::Pattern<detail::First>(std::make_shared<detail::First>());
  inline const auto End = detail::Pattern<detail::Last>(std::make_shared<detail::Last>());

  inline detail::Pattern<detail::TokenMatch> T(const Token& type)
  {
    return {std::make_shared<detail::TokenMatch>(type)};
  }

  inline detail::Pattern<detail::RegexMatch> T(const Token& type, const std::string& r)
  {
    return {std::make_shared<detail::RegexMatch>(type, r)};
  }

  inline detail::Pattern<detail::Inside> In(const Token& type)
  {
    return {std::make_shared<detail::Inside>(type)};
  }

  template<typename... Ts>
  inline detail::Pattern<detail::InsideN>
  In(const Token& type1, const Token& type2, const Ts&... types)
  {
    std::vector<Token> t = {type1, type2, types...};
    return {std::make_shared<detail::InsideN>(t)};
  }

  inline detail::EphemeralNode operator-(Node node)
  {
    return {node};
  }

  inline detail::EphemeralNodeRange operator-(NodeRange node)
  {
    return {node};
  }

  inline detail::RangeContents operator*(NodeRange range)
  {
    return {range};
  }

  inline detail::RangeOr operator||(NodeRange range, Node node)
  {
    return {range, node};
  }

  inline Node operator||(Node lhs, Node rhs)
  {
    return lhs ? lhs : rhs;
  }

  inline Node operator<<(Node node1, Node node2)
  {
    node1->push_back(node2);
    return node1;
  }

  inline Node operator<<(Node node, detail::EphemeralNode ephemeral)
  {
    node->push_back_ephemeral(ephemeral.node);
    return node;
  }

  inline Node operator<<(Node node, NodeRange range)
  {
    node->push_back(range);
    return node;
  }

  inline Node operator<<(Node node, detail::EphemeralNodeRange ephemeral)
  {
    node->push_back_ephemeral(ephemeral.range);
    return node;
  }

  inline Node operator<<(Node node, detail::RangeContents range_contents)
  {
    for (auto it = range_contents.range.first;
         it != range_contents.range.second;
         ++it)
    {
      node->push_back({(*it)->begin(), (*it)->end()});
    }

    return node;
  }

  inline Node operator<<(Node node, detail::RangeOr range_or)
  {
    if (range_or.range.first != range_or.range.second)
      node->push_back(range_or.range);
    else
      node->push_back(range_or.node);

    return node;
  }

  inline Node operator<<(Node node, Nodes range)
  {
    node->push_back({range.begin(), range.end()});
    return node;
  }

  inline Node operator^(const Token& type, Node node)
  {
    return NodeDef::create(type, node->location());
  }

  inline Node operator^(const Token& type, Location loc)
  {
    return NodeDef::create(type, loc);
  }

  inline Node operator^(const Token& type, const std::string& text)
  {
    return NodeDef::create(type, Location(text));
  }

  inline Node clone(Node node)
  {
    if (node)
      return node->clone();
    else
      return {};
  }

  inline Nodes clone(NodeRange range)
  {
    Nodes nodes;
    nodes.reserve(std::distance(range.first, range.second));

    for (auto it = range.first; it != range.second; ++it)
      nodes.push_back((*it)->clone());

    return nodes;
  }
}
