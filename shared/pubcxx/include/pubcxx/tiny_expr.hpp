#pragma once

#include <cassert>
#include <cmath>   // For mathematical functions
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/analyze.hpp>

namespace calculator {
    double calculate(std::string const& expr);

    namespace internal {
        namespace pegtl = TAO_PEGTL_NAMESPACE;

        enum class order : int {};

        struct op {
            order p;
            std::function<double(double, double)> f;
        };

        struct func {
            std::function<double(double)> f;
        };

        struct functions {
            std::map<std::string, func> m_funcs;

            functions() {
                insert("sin", [](double const d) { return std::sin(d); });
                insert("cos", [](double const d) { return std::cos(d); });
                insert("tan", [](double const d) { return std::tan(d); });
                insert("log", [](double const d) {
                    if (d <= 0.0) throw std::runtime_error("Logarithm of non-positive number");
                    return std::log(d);
                });
                insert("exp", [](double const d) { return std::exp(d); });
                insert("sqr", [](double const d) { return d * d; });
                insert("sqrt", [](double const d) {
                    if (d < 0.0) throw std::runtime_error("Square root of negative number");
                    return std::sqrt(d);
                });
                insert("csc", [](double const d) {
                    if (std::fabs(std::sin(d)) < 1e-9) throw std::runtime_error("Cosecant of zero is undefined");
                    return 1.0 / std::sin(d);
                });
                insert("sec", [](double const d) {
                    if (std::fabs(std::cos(d)) < 1e-9) throw std::runtime_error("Secant of zero is undefined");
                    return 1.0 / std::cos(d);
                });
                insert("cot", [](double const d) {
                    if (std::fabs(std::tan(d)) < 1e-9) throw std::runtime_error("Cotangent of zero is undefined");
                    return 1.0 / std::tan(d);
                });
            }

            void insert(std::string const& name, std::function<double(double)> const& f) {
                assert(!name.empty());
                m_funcs.try_emplace(name, func{f});
            }
        };

        struct stack {
            void push_op(op const& b) {
                while ((!m_o.empty()) && (m_o.back().p <= b.p)) { reduce(); }
                m_o.push_back(b);
            }

            void push_val(double const d) { m_l.push_back(d); }

            void apply_unary(char const c) {
                assert(!m_l.empty());
                if (c == '-') { m_l.back() = -m_l.back(); }
            }

            double finish()   // Changed return type to double
            {
                while (!m_o.empty()) { reduce(); }
                assert(m_l.size() == 1);
                auto const r = m_l.back();
                m_l.clear();
                m_o.clear();
                return r;
            }

            size_t val_size() const { return m_l.size(); }

            double val_back() const { return m_l.back(); }   // Changed return type to double

            void val_pop_back() { m_l.pop_back(); }

        private:
            std::vector<op> m_o;
            std::vector<double> m_l;

            void reduce() {
                assert(!m_o.empty());
                assert(m_l.size() > 1);

                auto const r = m_l.back();
                m_l.pop_back();
                auto const l = m_l.back();
                m_l.pop_back();
                auto const o = m_o.back();
                m_o.pop_back();             // Pop the operator
                m_l.push_back(o.f(l, r));   // Push the result
            }
        };

        struct stacks {
            std::map<std::string, op> const& m_ops;
            std::map<std::string, func> const& m_funcs;
            std::vector<stack> m_v;
            char m_pending_unary_op = 0;
            std::vector<std::string> m_pending_func_names;   // Use a vector for nesting

            explicit stacks(std::map<std::string, op> const& ops, std::map<std::string, func> const& funcs) :
                m_ops(ops), m_funcs(funcs) {
                open();
            }

            void open() { m_v.emplace_back(); }

            void close() {
                assert(m_v.size() > 1);
                auto const r = m_v.back().finish();
                m_v.pop_back();
                push_val(r);
            }

            void push_op(op const& o) {
                assert(!m_v.empty());
                m_v.back().push_op(o);
            }

            void push_val(double d) {
                assert(!m_v.empty());
                m_v.back().push_val(d);
            }

            void apply_unary(char c) {
                assert(!m_v.empty());
                m_v.back().apply_unary(c);
            }

            double finish() {
                assert(m_v.size() == 1);
                return m_v.back().finish();
            }
        };

        struct operators {
            std::map<std::string, op> m_ops;

            operators() {
                insert("*", order(5), [](double const l, double const r) { return l * r; });
                insert("/", order(5), [](double const l, double const r) {
                    if (r == 0.0) throw std::runtime_error("Division by zero");
                    return l / r;
                });
                insert("%", order(5), [](double const l, double const r) {
                    if (r == 0.0) throw std::runtime_error("Modulo by zero");
                    return static_cast<double>(static_cast<long>(l) % static_cast<long>(r));
                });
                insert("+", order(6), [](double const l, double const r) { return l + r; });
                insert("-", order(6), [](double const l, double const r) { return l - r; });
                insert("<<", order(7), [](double const l, double const r) {
                    return static_cast<double>(static_cast<long long>(l) << static_cast<long long>(r));
                });
                insert(">>", order(7), [](double const l, double const r) {
                    return static_cast<double>(static_cast<long long>(l) >> static_cast<long long>(r));
                });
                insert("<", order(8), [](double const l, double const r) { return (l < r) ? 1.0 : 0.0; });
                insert(">", order(8), [](double const l, double const r) { return (l > r) ? 1.0 : 0.0; });
                insert("<=", order(8), [](double const l, double const r) { return (l <= r) ? 1.0 : 0.0; });
                insert(">=", order(8), [](double const l, double const r) { return (l >= r) ? 1.0 : 0.0; });
                insert("==", order(9), [](double const l, double const r) { return (l == r) ? 1.0 : 0.0; });
                insert("!=", order(9), [](double const l, double const r) { return (l != r) ? 1.0 : 0.0; });
                insert("&", order(10), [](double const l, double const r) {
                    return static_cast<double>(static_cast<long long>(l) & static_cast<long long>(r));
                });
                insert("^", order(11), [](double const l, double const r) {
                    return static_cast<double>(static_cast<long long>(l) ^ static_cast<long long>(r));
                });
                insert("|", order(12), [](double const l, double const r) {
                    return static_cast<double>(static_cast<long long>(l) | static_cast<long long>(r));
                });
                insert("&&", order(13),
                       [](double const l, double const r) { return (l != 0.0 && r != 0.0) ? 1.0 : 0.0; });
                insert("||", order(14),
                       [](double const l, double const r) { return (l != 0.0 || r != 0.0) ? 1.0 : 0.0; });
            }

            void insert(std::string const& name, order const p, std::function<double(double, double)> const& f) {
                assert(!name.empty());
                m_ops.try_emplace(name, op{p, f});
            }
        };

        //--- Grammar Definition (with explicit pegtl:: prefixes) ---//
        struct expression;

        struct single_line_comment : pegtl::seq<pegtl::one<'#'>, pegtl::until<pegtl::eolf>> {};

        struct multi_line_comment : pegtl::seq<pegtl::string<'/', '*'>, pegtl::until<pegtl::string<'*', '/'>>> {};

        struct comment : pegtl::sor<single_line_comment, multi_line_comment> {};

        struct ignored : pegtl::sor<pegtl::space, comment> {};

        struct constant_pi : pegtl::string<'P', 'I'> {};

        struct constant_e : pegtl::string<'E'> {};

        struct function_name :
            pegtl::seq<pegtl::not_at<constant_pi>, pegtl::not_at<constant_e>,
                       pegtl::seq<pegtl::alpha, pegtl::star<pegtl::alnum>>> {};

        struct function_call :
            pegtl::seq<function_name, pegtl::one<'('>, pegtl::pad<expression, ignored>, pegtl::one<')'>> {};

        struct number :
            pegtl::seq<pegtl::plus<pegtl::digit>, pegtl::opt<pegtl::seq<pegtl::one<'.'>, pegtl::plus<pegtl::digit>>>> {
        };

        struct infix_op {
            using rule_t = pegtl::ascii::any::rule_t;

            template <pegtl::apply_mode, pegtl::rewind_mode, template <typename...> class Action,
                      template <typename...> class Control, typename ParseInput>
            static bool match(ParseInput& in, stacks const& s) {
                std::string_view best_match;
                for (auto const& op_pair : s.m_ops) {
                    auto const& op_str = op_pair.first;
                    if (op_str.length() > best_match.length() && in.size(op_str.length()) >= op_str.length()) {
                        if (std::string_view(in.current(), op_str.length()) == op_str) { best_match = op_str; }
                    }
                }

                if (!best_match.empty()) {
                    in.bump(best_match.length());
                    return true;
                }
                return false;
            }
        };

        struct unary_op : pegtl::one<'+', '-'> {};

        struct primary :
            pegtl::sor<function_call, number, constant_pi, constant_e,
                       pegtl::seq<pegtl::one<'('>, expression, pegtl::one<')'>>> {};

        struct unary : pegtl::seq<pegtl::opt<unary_op>, pegtl::pad<primary, ignored>> {};

        struct expression : pegtl::seq<unary, pegtl::star<pegtl::seq<pegtl::pad<infix_op, ignored>, unary>>> {};

        struct grammar : pegtl::seq<pegtl::pad<expression, ignored>, pegtl::eof> {};

        //--- Actions (with explicit pegtl:: prefixes where needed) ---//
        template <typename Rule>
        struct action : pegtl::nothing<Rule> {};

        template <>
        struct action<number> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.push_val(std::stod(in.string()));   // Changed to stod
                if (s.m_pending_unary_op != 0) {      // If there's a pending unary op
                    s.apply_unary(s.m_pending_unary_op);
                    s.m_pending_unary_op = 0;   // Reset
                }
            }
        };

        template <>
        struct action<constant_pi> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.push_val(M_PI);
                if (s.m_pending_unary_op != 0) {
                    s.apply_unary(s.m_pending_unary_op);
                    s.m_pending_unary_op = 0;
                }
            }
        };

        template <>
        struct action<constant_e> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.push_val(M_E);
                if (s.m_pending_unary_op != 0) {
                    s.apply_unary(s.m_pending_unary_op);
                    s.m_pending_unary_op = 0;
                }
            }
        };

        template <>
        struct action<unary_op> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.m_pending_unary_op = in.peek_char();   // Store the operator
            }
        };

        template <>
        struct action<infix_op> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.push_op(s.m_ops.at(in.string()));
            }
        };

        template <>
        struct action<function_name> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                s.m_pending_func_names.push_back(in.string());   // Push the name
            }
        };

        template <>
        struct action<function_call> {
            template <typename ActionInput>
            static void apply(ActionInput const& in, stacks& s) {
                assert(!s.m_v.empty());
                assert(s.m_v.back().val_size() >= 1);      // Ensure argument is on stack
                assert(!s.m_pending_func_names.empty());   // Ensure there's a pending function name

                auto const arg = s.m_v.back().val_back();
                s.m_v.back().val_pop_back();

                auto const func_name = s.m_pending_func_names.back();   // Get the last pushed name
                s.m_pending_func_names.pop_back();                      // Pop it

                auto const& func_impl = s.m_funcs.at(func_name);   // Use the popped name
                s.push_val(func_impl.f(arg));
            }
        };

        template <>
        struct action<pegtl::one<'('>> {
            template <typename ActionInput, typename... States>
            static void apply(ActionInput const& /*unused*/, States&&... s) {
                (s.open(), ...);
            }
        };

        template <>
        struct action<pegtl::one<')'>> {
            template <typename ActionInput, typename... States>
            static void apply(ActionInput const& /*unused*/, States&&... s) {
                (s.close(), ...);
                auto apply_pending = [](auto& state) {
                    if (state.m_pending_unary_op != 0) {
                        state.apply_unary(state.m_pending_unary_op);
                        state.m_pending_unary_op = 0;
                    }
                };
                (apply_pending(s), ...);   // Apply to each state
            }
        };

    }   // namespace internal

    inline double calculate(std::string const& expr) {
        namespace pegtl = TAO_PEGTL_NAMESPACE;

        static bool const grammar_ok = (pegtl::analyze<internal::grammar>() == 0);
        if (!grammar_ok) { throw std::runtime_error("Internal grammar analysis failed."); }

        internal::operators ops;
        internal::functions funcs;
        internal::stacks s(ops.m_ops, funcs.m_funcs);

        pegtl::string_input in(expr, "calculator_input");
        if (!pegtl::parse<internal::grammar, internal::action>(in, s)) {
            throw std::runtime_error("Parse error: invalid expression");
        }

        return s.finish();
    }

}   // namespace calculator
