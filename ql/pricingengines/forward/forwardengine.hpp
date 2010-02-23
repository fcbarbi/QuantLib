/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2002, 2003, 2004 Ferdinando Ametrano
 Copyright (C) 2007 StatPro Italia srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file forwardengine.hpp
    \brief Forward (strike-resetting) vanilla-option engine
*/

#ifndef quantlib_forward_engine_hpp
#define quantlib_forward_engine_hpp

#include <ql/instruments/forwardvanillaoption.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/termstructures/volatility/equityfx/impliedvoltermstructure.hpp>
#include <ql/termstructures/yield/impliedtermstructure.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/exercise.hpp>

namespace QuantLib {

    //! %Forward engine for vanilla options
    /*! \ingroup forwardengines

        \test
        - the correctness of the returned value is tested by
          reproducing results available in literature.
        - the correctness of the returned greeks is tested by
          reproducing numerical derivatives.
    */
    template <class Engine>
    class ForwardVanillaEngine
        : public GenericEngine<ForwardOptionArguments<VanillaOption::arguments>,
                               VanillaOption::results> {
      public:
        ForwardVanillaEngine(
                    const boost::shared_ptr<GeneralizedBlackScholesProcess>&);
        void calculate() const;
      protected:
        void setup() const;
        void getOriginalResults() const;
        boost::shared_ptr<GeneralizedBlackScholesProcess> process_;
        mutable boost::shared_ptr<Engine> originalEngine_;
        mutable VanillaOption::arguments* originalArguments_;
        mutable const VanillaOption::results* originalResults_;
    };


    // template definitions

    template <class ArgumentsType>
    void ForwardOptionArguments<ArgumentsType>::validate() const {
        ArgumentsType::validate();

        QL_REQUIRE(moneyness != Null<Real>(), "null moneyness given");
        QL_REQUIRE(moneyness > 0.0, "negative or zero moneyness given");

        QL_REQUIRE(resetDate != Null<Date>(), "null reset date given");
        QL_REQUIRE(resetDate >= Settings::instance().evaluationDate(),
                   "reset date in the past");
        QL_REQUIRE(this->exercise->lastDate() > resetDate,
                   "reset date later or equal to maturity");
    }


    template <class Engine>
    ForwardVanillaEngine<Engine>::ForwardVanillaEngine(
        const boost::shared_ptr<GeneralizedBlackScholesProcess>& process)
    : process_(process) {
        registerWith(process_);
    }


    template <class Engine>
    void ForwardVanillaEngine<Engine>::setup() const {

        boost::shared_ptr<StrikedTypePayoff> argumentsPayoff =
            boost::dynamic_pointer_cast<StrikedTypePayoff>(
                this->arguments_.payoff);
        QL_REQUIRE(argumentsPayoff, "wrong payoff given");

        boost::shared_ptr<StrikedTypePayoff> payoff(
                   new PlainVanillaPayoff(argumentsPayoff->optionType(),
                                          this->arguments_.moneyness *
                                          process_->x0()));

        // maybe the forward value is "better", in some fashion
        // the right level is needed in order to interpolate
        // the vol
        Handle<Quote> spot = process_->stateVariable();
        QL_REQUIRE(spot->value() >= 0.0, "negative or null underlting given");
        Handle<YieldTermStructure> dividendYield(
            boost::shared_ptr<YieldTermStructure>(
               new ImpliedTermStructure(process_->dividendYield(),
                                        this->arguments_.resetDate)));
        Handle<YieldTermStructure> riskFreeRate(
            boost::shared_ptr<YieldTermStructure>(
               new ImpliedTermStructure(process_->riskFreeRate(),
                                        this->arguments_.resetDate)));
        // The following approach is ok if the vol is at most
        // time dependant. It is plain wrong if it is asset dependant.
        // In the latter case the right solution would be stochastic
        // volatility or at least local volatility (which unfortunately
        // implies an unrealistic time-decreasing smile)
        Handle<BlackVolTermStructure> blackVolatility(
            boost::shared_ptr<BlackVolTermStructure>(
                new ImpliedVolTermStructure(process_->blackVolatility(),
                                            this->arguments_.resetDate)));

        boost::shared_ptr<GeneralizedBlackScholesProcess> fwdProcess(
                       new GeneralizedBlackScholesProcess(spot, dividendYield,
                                                          riskFreeRate,
                                                          blackVolatility));

        originalEngine_ = boost::shared_ptr<Engine>(new Engine(fwdProcess));

        originalArguments_ =
            dynamic_cast<VanillaOption::arguments*>(
                                             originalEngine_->getArguments());
        QL_REQUIRE(originalArguments_, "wrong engine type");
        originalResults_ =
            dynamic_cast<const VanillaOption::results*>(
                                               originalEngine_->getResults());
        QL_REQUIRE(originalResults_, "wrong engine type");

        originalArguments_->payoff = payoff;
        originalArguments_->exercise = this->arguments_.exercise;

        originalArguments_->validate();
    }

    template <class Engine>
    void ForwardVanillaEngine<Engine>::calculate() const {
        setup();
        originalEngine_->calculate();
        getOriginalResults();
    }

    template <class Engine>
    void ForwardVanillaEngine<Engine>::getOriginalResults() const {

        DayCounter rfdc = process_->riskFreeRate()->dayCounter();
        DayCounter divdc = process_->dividendYield()->dayCounter();
        Time resetTime = rfdc.yearFraction(
                                     process_->riskFreeRate()->referenceDate(),
                                     this->arguments_.resetDate);
        DiscountFactor discQ = process_->dividendYield()->discount(
                                                  this->arguments_.resetDate);

        this->results_.value = discQ * originalResults_->value;
        // I need the strike derivative here ...
        this->results_.delta = discQ * (originalResults_->delta +
            this->arguments_.moneyness * originalResults_->strikeSensitivity);
        this->results_.gamma = 0.0;
        this->results_.theta = process_->dividendYield()->
            zeroRate(this->arguments_.resetDate, divdc, Continuous, NoFrequency)
            * this->results_.value;
        this->results_.vega  = discQ * originalResults_->vega;
        this->results_.rho   = discQ *  originalResults_->rho;
        this->results_.dividendRho = - resetTime * this->results_.value
            + discQ * originalResults_->dividendRho;
    }

}


#endif
