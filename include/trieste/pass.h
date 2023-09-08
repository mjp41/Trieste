#pragma once

#include "rewrite.h"

namespace trieste
{
  namespace dir
  {
    using flag = uint32_t;
    constexpr flag bottomup = 1 << 0;
    constexpr flag topdown = 1 << 1;
    constexpr flag once = 1 << 2;
  };

  class PassDef;
  using Pass = std::shared_ptr<PassDef>;

  class PassDef
  {
  public:
    using F = std::function<size_t(Node)>;

  private:
    F pre_once;
    F post_once;
    
    // Flag specifies if pre_ is empty.
    bool pre_set_ = false;
    std::map<Token, F> pre_;

    bool post_set_ = false;
    std::map<Token, F> post_;
    dir::flag direction_;
    std::vector<detail::RewritePtr> rules_;

  public:
    PassDef(dir::flag direction = dir::topdown) : direction_(direction) {}

    PassDef(const std::initializer_list<detail::RewritePtr>& r)
    : direction_(dir::topdown), rules_(r)
    {}

    PassDef(
      dir::flag direction,
      const std::initializer_list<detail::RewritePtr>& r)
    : direction_(direction), rules_(r)
    {}

    operator Pass() const
    {
      return std::make_shared<PassDef>(std::move(*this));
    }

    void pre(F f)
    {
      pre_once = f;
    }

    void post(F f)
    {
      post_once = f;
    }

    void pre(const Token& type, F f)
    {
      pre_set_ = true;
      pre_[type] = f;
    }

    void post(const Token& type, F f)
    {
      post_set_ = true;
      post_[type] = f;
    }

    template<typename... Ts>
    void rules(Ts... r)
    {
      std::vector<detail::RewritePtr> rules = {r...};
      rules_.insert(rules_.end(), rules.begin(), rules.end());
    }

    void rules(const std::initializer_list<detail::RewritePtr>& r)
    {
      rules_.insert(rules_.end(), r.begin(), r.end());
    }

    std::tuple<Node, size_t, size_t> run(Node node)
    {
      size_t changes = 0;
      size_t changes_sum = 0;
      size_t count = 0;

      if (pre_once)
        changes_sum += pre_once(node);

      // Because apply runs over child nodes, the top node is never visited.
      Match match(node);
      do
      {
        changes = apply(match, node);

        auto lifted = lift(node);
        if (!lifted.empty())
          throw std::runtime_error("lifted nodes with no destination");

        changes_sum += changes;
        count++;

        if (flag(dir::once))
          break;
      } while (changes > 0);

      if (post_once)
        changes_sum += post_once(node);

      return {node, count, changes_sum};
    }

  private:
    bool flag(dir::flag f) const
    {
      return (direction_ & f) != 0;
    }

    SNMALLOC_FAST_PATH void step(Match& match, const Node& node, NodeIt& it, size_t& changes, ptrdiff_t& replaced)
    {
        for (auto& rule : rules_)
        {
          auto start = it;
          match.reset();
          if (rule->apply(match, node, start, it, changes, replaced))
            return;
          it = start;
        }
    }

    size_t apply(Match& match, const Node& node)
    {
      if (node->type().in({Error, Lift}))
        return 0;

      size_t changes = 0;

      if (SNMALLOC_UNLIKELY(pre_set_))
      {
        auto pre_f = pre_.find(node->type());
        if (pre_f != pre_.end())
          changes += pre_f->second(node);
      }

      auto it = node->begin();

      while (it != node->end())
      {
        // Don't examine Error or Lift nodes.
        if ((*it)->type().in({Error, Lift}))
        {
          ++it;
          continue;
        }

        if (flag(dir::bottomup))
          changes += apply(match, *it);

        ptrdiff_t replaced = -1;

        step(match, node, it, changes, replaced);

        if (flag(dir::once))
        {
          if (flag(dir::topdown) && (replaced != 0))
          {
            // Move down the tree.
            auto to = std::max(replaced, ptrdiff_t(1));

            for (ptrdiff_t i = 0; i < to; ++i)
              changes += apply(match, *(it + i));
          }

          // Skip over everything we examined or populated.
          if (replaced >= 0)
            it += replaced;
          else
            ++it;
        }
        else if (replaced >= 0)
        {
          // If we did something, reexamine from the beginning.
          it = node->begin();
        }
        else
        {
          // If we did nothing, move down the tree.
          if (flag(dir::topdown))
            changes += apply(match, *it);

          // Advance to the next node.
          ++it;
        }
      }

      if (SNMALLOC_UNLIKELY(post_set_))
      {
        auto post_f = post_.find(node->type());
        if (post_f != post_.end())
          changes += post_f->second(node);
      }

      return changes;
    }

    Nodes lift(Node node)
    {
      Nodes uplift;
      auto it = node->begin();

      while (it != node->end())
      {
        bool advance = true;
        auto lifted = lift(*it);

        if ((*it)->type() == Lift)
        {
          lifted.insert(lifted.begin(), *it);
          it = node->erase(it, it + 1);
          advance = false;
        }

        for (auto& lnode : lifted)
        {
          if (lnode->front()->type() == node->type())
          {
            it = node->insert(it, lnode->begin() + 1, lnode->end());
            it += lnode->size() - 1;
            advance = false;
          }
          else
          {
            uplift.push_back(lnode);
          }
        }

        if (advance)
          ++it;
      }

      return uplift;
    }
  };
}
