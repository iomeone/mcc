// TODO: closure (wrapper) for external functions
#include <unordered_map>
#include <unordered_set>
#include <iostream>

#include "closure.h"
#include "typing.h"
#include "printer.h"

namespace mcc {
    namespace closure {
        using known_t = std::unordered_set<std::shared_ptr<identifier>>;
    }
}

namespace {
    using namespace mcc;
    using namespace mcc::closure;

    uint32_t num_closures = 0;

    using std::make_shared;
    using std::make_tuple;
    using std::make_pair;

    // pass
    struct pass {
        using result_type = ast;
    private:
        known_t & free_variable;
        known_t & callee_func;
        std::vector<mcc::closure::toplevel_t> & toplevel;
    public:
        pass(known_t & f, known_t & c, std::vector<mcc::closure::toplevel_t> & t) : free_variable(f), callee_func(c), toplevel(t) { }

        result_type operator() (const knormal::ast & e) {
            return std::visit(pass(free_variable, callee_func, toplevel), e);
        }

        template <typename T>
        result_type operator() (const T & e) {
            return e;
        }

        template <typename Op>
        result_type operator() (const sptr<knormal::unary<Op>> & e) {
            pass(free_variable, callee_func, toplevel)(e->value);
            return e;
        }

        template <typename Op>
        result_type operator() (const sptr<knormal::binary<Op>> & e) {
            pass(free_variable, callee_func, toplevel)(std::get<0>(e->value));
            pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            return e;
        }

        result_type operator() (const sptr<knormal::array> & e) {
            pass(free_variable, callee_func, toplevel)(std::get<0>(e->value));
            pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            return e;
        }

        result_type operator() (const sptr<knormal::branch> & e) {
            auto && ret = make_shared<branch>(make_tuple(std::get<0>(e->value),
                                                         pass(free_variable, callee_func, toplevel)(std::get<1>(e->value)),
                                                         pass(free_variable, callee_func, toplevel)(std::get<2>(e->value))));
            pass(free_variable, callee_func, toplevel)(std::get<0>(e->value));
            return ret;
        }

        result_type operator() (const sptr<knormal::identifier> & e) {
            auto && t = unwrap(std::get<1>(e->value));
            if (std::get_if<std::shared_ptr<type::function>>(&t)) {
                std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = true;
                if (e->is_external) {
                    // if the id is external function
                    // we must create function wrapper (maybe...?)
                    fprintf(stderr, "external function (%s) is bind to a variable.\n", std::get<0>(e->value).c_str());
                    assert(false);
                }
            }
            if (e->is_global || e->is_external) {
                /* */
            } else {
                free_variable.insert(e);
            }
            return e;
        }

        result_type operator() (const sptr<knormal::app> & e) {
            auto && ident = std::get<0>(e->value);
            auto func_type = std::get<std::shared_ptr<type::function>>(unwrap(std::get<1>(ident->value)));
            auto && xts = std::get<1>(e->value);
            // freevariable insert
            std::for_each(xts.cbegin(), xts.cend(), [this] (auto n) {
                    pass(free_variable, callee_func, toplevel)(n);
                });

            if (ident->is_external) {
                func_type->is_closure = false;
            } else {
                callee_func.insert(ident);
                if (func_type->is_closure) { // means closure
                    free_variable.insert(ident);
                }
            }
            return e;
        }

        result_type operator() (const sptr<knormal::tuple> & e) {
            for (auto i : e->value) {
                pass(free_variable, callee_func, toplevel)(i);
            }
            return e;
        }

        result_type operator() (const sptr<knormal::get> & e) {
            pass(free_variable, callee_func, toplevel)(std::get<0>(e->value));
            pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            return e;
        }

        result_type operator() (const sptr<knormal::put> & e) {
            pass(free_variable, callee_func, toplevel)(std::get<0>(e->value));
            pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            pass(free_variable, callee_func, toplevel)(std::get<2>(e->value));
            return e;
        }

        result_type operator() (const sptr<knormal::let> & e) {
            auto && ident = std::get<0>(e->value);
            auto && e1 = pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            auto && e2 = pass(free_variable, callee_func, toplevel)(std::get<2>(e->value));
            auto && new_let = make_shared<let>(make_tuple(ident, std::move(e1), std::move(e2)));
            free_variable.erase(ident);
            return new_let;
        }

        result_type operator() (const sptr<knormal::let_tuple> & e) {
            auto && ret = make_shared<let_tuple>(make_tuple(std::get<0>(e->value),
                                                            std::get<1>(e->value),
                                                            pass(free_variable, callee_func, toplevel)(std::get<2>(e->value))));
            for (auto && i : std::get<0>(e->value)) { free_variable.erase(i); }
            pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            return ret;
        }

        result_type operator() (const sptr<knormal::let_rec> & e) {
            auto && ident = std::get<0>(e->value);
            auto && t = std::get<1>(ident->value); // type
            auto && yts = std::get<1>(e->value); // args
            auto && e1 = std::get<2>(e->value); // fun body
            auto && e2 = std::get<3>(e->value); // exp
            known_t fv_body;
            known_t fn_body;
            // try as non-closure
            std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = false;
            auto && e1_ = pass(fv_body, fn_body, toplevel)(e1);
            // remove non-free variable in body
            for (auto && i : yts) { fv_body.erase(i); }
            // check freevariables in e1_
            if (!fv_body.empty()) {
                // fprintf(stderr, "function %s cannot be directly applied in fact@.\n", x.c_str());
                // for (auto && i : fv_body) {
                //     std::cerr << i << ',';
                // }
                // putchar('\n');
                // add function as a closure
                std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = true;
                if (fn_body.find(ident) != fn_body.end()) { // recursive closure
                    fv_body.insert(ident); // re-insert temporally erased fv
                }
            }
            // free_variable (and its type) in function
            std::vector<sptr<identifier>> zts;
            // required env for type setting below
            std::transform(fv_body.cbegin(), fv_body.cend(), std::back_inserter(zts), [&] (auto & z) {
                    return z;
                });
            auto && e2_ = pass(free_variable, callee_func, toplevel)(e2);

            ast ret;
            if ((free_variable.find(ident) != free_variable.end()) || fv_body.size()) {
                // see code/closure.ml
                std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = true;
                fprintf(stderr, "%u, creating closure for %s@.\n", num_closures, std::get<0>(ident->value).c_str());
                fprintf(stderr, "[fv]: ");
                std::for_each(fv_body.cbegin(), fv_body.cend(), [] (auto & i) {
                        mcc::printer::printer()(i);
                    });
                fprintf(stderr, "\n");
                ++num_closures;
                auto && cls = make_shared<let_rec>(make_tuple(ident,
                                                              std::vector<std::shared_ptr<identifier>>(fv_body.cbegin(), fv_body.cend()),
                                                              value::get_const_unit(),
                                                              e2_));
                std::copy(fv_body.begin(), fv_body.end(), std::inserter(free_variable, free_variable.end())); // union fv_body & free_variable
                std::copy(fn_body.begin(), fn_body.end(), std::inserter(callee_func, callee_func.end()));
                free_variable.erase(ident); // x is now made by let_rec
                ret = std::move(cls);
            } else {
                ret = e2_;
            }
            toplevel.emplace_back(make_shared<global_rec>(make_tuple(ident, yts, std::move(zts), e1_)));
            return ret;
        }

    };

    struct global_pass {
        using result_type = toplevel_t;
    private:
        known_t & free_variable;
        known_t & callee_func;
        std::vector<mcc::closure::toplevel_t> & toplevel;
    public:
        global_pass(known_t & f, known_t & c, std::vector<mcc::closure::toplevel_t> & t) : free_variable(f), callee_func(c), toplevel(t) { }

        result_type operator() (const sptr<knormal::external> & e) {
            return e;
        }

        result_type operator() (const sptr<knormal::global> & e) {
            auto && ident = std::get<0>(e->value);
            auto && e1 = pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            // free_variable?
            free_variable.erase(ident);
            return make_shared<global>(make_tuple(ident, std::move(e1)));
        }

        result_type operator() (const sptr<knormal::global_tuple> & e) {
            auto && e1 = pass(free_variable, callee_func, toplevel)(std::get<1>(e->value));
            auto && ret = make_shared<global_tuple>(make_tuple(std::get<0>(e->value), std::move(e1)));
            for (auto && i : std::get<0>(e->value)) { free_variable.erase(i); }
            return std::move(ret);
        }

        result_type operator() (const sptr<knormal::global_rec> & e) {
            auto && ident = std::get<0>(e->value);
            auto && t = std::get<1>(ident->value); // type
            auto && yts = std::get<1>(e->value); // args
            auto && e1 = std::get<3>(e->value); // fun body
            known_t fv_body;
            known_t fn_body;
            // try as non-closure
            std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = false;
            auto && e1_ = pass(fv_body, fn_body, toplevel)(e1);
            // remove non-free variable in bodyn
            for (auto && i : yts) { fv_body.erase(i); }
            // check freevariables in e1_
            if (!fv_body.empty()) {
                // fprintf(stderr, "function %s cannot be directly applied in fact@.\n", x.c_str());
                // for (auto && i : fv_body) {
                //     std::cerr << i << ',';
                // }
                // putchar('\n');
                // add function as a closure
                std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = true;
                if (fn_body.find(ident) != fn_body.end()) { // recursive closure
                    fv_body.insert(ident); // re-insert temporally erased fv
                }
            }
            // free_variable (and its type) in function
            std::vector<sptr<identifier>> zts;
            // required env for type setting below
            std::transform(fv_body.cbegin(), fv_body.cend(), std::back_inserter(zts), [&] (auto & z) {
                    return z;
                });

            ast ret;
            if ((free_variable.find(ident) != free_variable.end()) || fv_body.size()) {
                // see code/closure.ml
                std::get<std::shared_ptr<type::function>>(unwrap(t))->is_closure = true;
                auto && cls = make_shared<let_rec>(make_tuple(ident,
                                                              std::vector<std::shared_ptr<identifier>>(fv_body.cbegin(), fv_body.cend()),
                                                              value::get_const_unit(),
                                                              value::get_const_unit()));
                std::copy(fv_body.begin(), fv_body.end(), std::inserter(free_variable, free_variable.end())); // union fv_body & free_variable
                std::copy(fn_body.begin(), fn_body.end(), std::inserter(callee_func, callee_func.end()));
                free_variable.erase(ident); // x is now made by let_rec
                ret = std::move(cls);
            } else {
                ret = value::get_const_unit();
            }
            toplevel.emplace_back(make_shared<global_rec>(make_tuple(ident, yts, std::move(zts), e1_)));
            return ret;
        }

        template <typename T>
        result_type operator() (const T & ast) {
            auto && ret = std::visit(pass(free_variable, callee_func, toplevel), ast);
            return ret;
        }

    };

}

std::vector<mcc::closure::toplevel_t> mcc::closure::f(std::vector<mcc::knormal::toplevel_t> && ast) {
    mcc::closure::known_t free_variable;
    mcc::closure::known_t callee_func;
    std::vector<mcc::closure::toplevel_t> ret;
    for (auto && t : ast) {
        ret.push_back(std::visit(global_pass(free_variable, callee_func, ret), t));
    }
    fprintf(stderr, "closures: %u\n", num_closures);
    return ret;
}
