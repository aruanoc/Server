// Copyright (c) Microsoft Open Technologies, Inc. All rights reserved. See License.txt in the project root for license information.

#pragma once

#if !defined(RXCPP_OPERATORS_RX_SKIP_UNTIL_HPP)
#define RXCPP_OPERATORS_RX_SKIP_UNTIL_HPP

#include "../rx-includes.hpp"

namespace rxcpp {

namespace operators {

namespace detail {

template<class T, class Observable, class TriggerObservable, class Coordination>
struct skip_until : public operator_base<T>
{
    typedef typename std::decay<Observable>::type source_type;
    typedef typename std::decay<TriggerObservable>::type trigger_source_type;
    typedef typename std::decay<Coordination>::type coordination_type;
    typedef typename coordination_type::coordinator_type coordinator_type;
    struct values
    {
        values(source_type s, trigger_source_type t, coordination_type sf)
            : source(std::move(s))
            , trigger(std::move(t))
            , coordination(std::move(sf))
        {
        }
        source_type source;
        trigger_source_type trigger;
        coordination_type coordination;
    };
    values initial;

    skip_until(source_type s, trigger_source_type t, coordination_type sf)
        : initial(std::move(s), std::move(t), std::move(sf))
    {
    }

    struct mode
    {
        enum type {
            skipping,  // no messages from trigger
            clear,     // trigger completed
            triggered, // trigger sent on_next
            errored,   // error either on trigger or on observable
            stopped    // observable completed
        };
    };

    template<class Subscriber>
    void on_subscribe(Subscriber s) const {

        typedef Subscriber output_type;
        struct state_type
            : public std::enable_shared_from_this<state_type>
            , public values
        {
            state_type(const values& i, coordinator_type coor, const output_type& oarg)
                : values(i)
                , mode_value(mode::skipping)
                , coordinator(std::move(coor))
                , out(oarg)
            {
                out.add(trigger_lifetime);
                out.add(source_lifetime);
            }
            typename mode::type mode_value;
            composite_subscription trigger_lifetime;
            composite_subscription source_lifetime;
            coordinator_type coordinator;
            output_type out;
        };

        auto coordinator = initial.coordination.create_coordinator();

        // take a copy of the values for each subscription
        auto state = std::shared_ptr<state_type>(new state_type(initial, std::move(coordinator), std::move(s)));

        auto trigger = on_exception(
            [&](){return state->coordinator.in(state->trigger);},
            state->out);
        if (trigger.empty()) {
            return;
        }

        auto source = on_exception(
            [&](){return state->coordinator.in(state->source);},
            state->out);
        if (source.empty()) {
            return;
        }

        auto sinkTrigger = make_subscriber<typename trigger_source_type::value_type>(
        // share parts of subscription
            state->out,
        // new lifetime
            state->trigger_lifetime,
        // on_next
            [state](const typename trigger_source_type::value_type&) {
                if (state->mode_value != mode::skipping) {
                    return;
                }
                state->mode_value = mode::triggered;
                state->trigger_lifetime.unsubscribe();
            },
        // on_error
            [state](std::exception_ptr e) {
                if (state->mode_value != mode::skipping) {
                    return;
                }
                state->mode_value = mode::errored;
                state->out.on_error(e);
            },
        // on_completed
            [state]() {
                if (state->mode_value != mode::skipping) {
                    return;
                }
                state->mode_value = mode::clear;
                state->trigger_lifetime.unsubscribe();
            }
        );
        auto selectedSinkTrigger = on_exception(
            [&](){return state->coordinator.out(sinkTrigger);},
            state->out);
        if (selectedSinkTrigger.empty()) {
            return;
        }
        trigger->subscribe(std::move(selectedSinkTrigger.get()));

        source.get().subscribe(
        // split subscription lifetime
            state->source_lifetime,
        // on_next
            [state](T t) {
                if (state->mode_value != mode::triggered) {
                    return;
                }
                state->out.on_next(t);
            },
        // on_error
            [state](std::exception_ptr e) {
                if (state->mode_value > mode::triggered) {
                    return;
                }
                state->mode_value = mode::errored;
                state->out.on_error(e);
            },
        // on_completed
            [state]() {
                if (state->mode_value != mode::triggered) {
                    return;
                }
                state->mode_value = mode::stopped;
                state->out.on_completed();
            }
        );
    }
};

template<class TriggerObservable, class Coordination>
class skip_until_factory
{
    typedef typename std::decay<TriggerObservable>::type trigger_source_type;
    typedef typename std::decay<Coordination>::type coordination_type;

    trigger_source_type trigger_source;
    coordination_type coordination;
public:
    skip_until_factory(trigger_source_type t, coordination_type sf)
        : trigger_source(std::move(t))
        , coordination(std::move(sf))
    {
    }
    template<class Observable>
    auto operator()(Observable&& source)
        ->      observable<typename std::decay<Observable>::type::value_type, skip_until<typename std::decay<Observable>::type::value_type, Observable, trigger_source_type, Coordination>> {
        return  observable<typename std::decay<Observable>::type::value_type, skip_until<typename std::decay<Observable>::type::value_type, Observable, trigger_source_type, Coordination>>(
                                                                              skip_until<typename std::decay<Observable>::type::value_type, Observable, trigger_source_type, Coordination>(std::forward<Observable>(source), trigger_source, coordination));
    }
};

}

template<class TriggerObservable, class Coordination>
auto skip_until(TriggerObservable&& t, Coordination&& sf)
    ->      detail::skip_until_factory<TriggerObservable, Coordination> {
    return  detail::skip_until_factory<TriggerObservable, Coordination>(std::forward<TriggerObservable>(t), std::forward<Coordination>(sf));
}

}

}

#endif
