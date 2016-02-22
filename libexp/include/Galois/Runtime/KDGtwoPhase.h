/** KDG two phase executor -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * @author M. Amber Hassaan <ahassaan@ices.utexas.edu>
 */

#ifndef GALOIS_RUNTIME_KDGTWOPHASE_H
#define GALOIS_RUNTIME_KDGTWOPHASE_H

#include "Galois/GaloisForwardDecl.h"
#include "Galois/Accumulator.h"
#include "Galois/Atomic.h"
#include "Galois/BoundedVector.h"
#include "Galois/gdeque.h"
#include "Galois/PriorityQueue.h"
#include "Galois/Timer.h"
#include "Galois/DoAllWrap.h"
#include "Galois/PerThreadContainer.h"
#include "Galois/optional.h"

#include "Galois/Substrate/Barrier.h"
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/Executor_DoAll.h"
#include "Galois/Runtime/Executor_ParaMeter.h"
#include "Galois/Runtime/ForEachTraits.h"
#include "Galois/Runtime/Range.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Substrate/Termination.h"
#include "Galois/Substrate/ThreadPool.h"
#include "Galois/Runtime/KDGtwoPhaseSupport.h"
#include "Galois/Runtime/WindowWorkList.h"
#include "Galois/Runtime/UserContextAccess.h"
#include "Galois/Substrate/gio.h"
#include "Galois/Runtime/ThreadRWlock.h"
#include "Galois/Substrate/CompilerSpecific.h"
#include "Galois/Runtime/Mem.h"

#include <boost/iterator/transform_iterator.hpp>

#include <iostream>
#include <memory>


namespace Galois {
namespace Runtime {


namespace {
template <typename T, typename Cmp, typename NhFunc, typename ExFunc, typename OpFunc, typename WindowWL, bool NEEDS_PUSH, bool HAS_EXEC_FUNC, bool ENABLE_PARAMETER> 
class IKDGtwoPhaseExecutor {
public:
  using Ctxt = TwoPhaseContext<T, Cmp>;
  using CtxtAlloc = FixedSizeAllocator<Ctxt>;
  using CtxtWL = PerThreadBag<Ctxt*>;

  using UserCtxt = UserContextAccess<T>;
  using PerThreadUserCtxt = Substrate::PerThreadStorage<UserCtxt>;

protected:

  static const bool DETAILED_STATS = false;

  struct MakeContext {
    const Cmp& cmp;
    CtxtAlloc& ctxtAlloc;

    MakeContext (const Cmp& cmp, CtxtAlloc& ctxtAlloc)
      : cmp (cmp), ctxtAlloc (ctxtAlloc)
    {}

    Ctxt* operator () (const T& x) {
      Ctxt* ctx = ctxtAlloc.allocate (1);
      assert (ctx != nullptr);
      ctxtAlloc.construct (ctx, x, cmp);

      return ctx;
    }
  };

  Cmp cmp;
  NhFunc nhFunc;
  ExFunc execFunc;
  OpFunc opFunc;
  const char* loopname;
  WindowWL winWL;
  CtxtAlloc ctxtAlloc;
  MakeContext ctxtMaker;
  std::unique_ptr<CtxtWL> currWL;
  std::unique_ptr<CtxtWL> nextWL;


  size_t windowSize;
  size_t rounds;
  size_t totalTasks;
  size_t totalCommits;
  double targetCommitRatio;

  PerThreadUserCtxt userHandles;
  GAccumulator<size_t> roundTasks;
  GAccumulator<size_t> roundCommits;

public:
  IKDGtwoPhaseExecutor (
      const Cmp& cmp, 
      const NhFunc& nhFunc,
      const ExFunc& execFunc,
      const OpFunc& opFunc,
      const char* ln)
    :
      cmp (cmp),
      nhFunc (nhFunc),
      execFunc (execFunc),
      opFunc (opFunc),
      loopname (ln),
      winWL (cmp),
      ctxtMaker (cmp, ctxtAlloc),
      currWL (new CtxtWL),
      nextWL (new CtxtWL),
      windowSize(0),
      rounds(0),
      totalTasks(0),
      totalCommits(0),
      targetCommitRatio (commitRatioArg)
  {
    if (targetCommitRatio < 0.0) {
      targetCommitRatio = 0.0;
    }
    if (targetCommitRatio > 1.0) {
      targetCommitRatio = 1.0;
    }

    if (ENABLE_PARAMETER) {
      assert (targetCommitRatio == 0.0);
    }

    if (loopname == nullptr) {
      loopname = "NULL";
    }
  }

  ~IKDGtwoPhaseExecutor () {

    if (ENABLE_PARAMETER) {
      ParaMeter::closeStatsFile ();
    }

    printStats ();
    assert (currWL->empty_all ());
    assert (nextWL->empty_all ());
  }

  template <typename R>
  void push_initial (const R& range) {
    if (targetCommitRatio == 0.0) {

      Galois::do_all_choice (range,
          [this] (const T& x) {
            nextWL->push_back (ctxtMaker (x));
          }, 
          "init-fill",
          chunk_size<NhFunc::CHUNK_SIZE> ());


    } else {
      winWL.initfill (range);
          
    }
  }

  void execute () {
    execute_impl ();
  }

protected:
  GALOIS_ATTRIBUTE_PROF_NOINLINE void spillAll (CtxtWL& wl) {
    assert (targetCommitRatio != 0.0);
    on_each(
        [this, &wl] (const unsigned tid, const unsigned numT) {
          while (!wl[tid].empty ()) {
            Ctxt* c = wl[tid].back ();
            wl[tid].pop_back ();

            winWL.push (c->getActive ());
            c->~Ctxt ();
            ctxtAlloc.deallocate (c, 1);
          }
        });

    assert (wl.empty_all ());
    assert (!winWL.empty ());
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void refill (CtxtWL& wl, size_t currCommits, size_t prevWindowSize) {

    assert (targetCommitRatio != 0.0);

    const size_t INIT_MAX_ROUNDS = 500;
    const size_t THREAD_MULT_FACTOR = 16;
    const double TARGET_COMMIT_RATIO = targetCommitRatio;
    const size_t MIN_WIN_SIZE = OpFunc::CHUNK_SIZE * getActiveThreads ();
    // const size_t MIN_WIN_SIZE = 2000000; // OpFunc::CHUNK_SIZE * getActiveThreads ();
    const size_t WIN_OVER_SIZE_FACTOR = 8;

    if (prevWindowSize == 0) {
      assert (currCommits == 0);

      // initial settings
      if (NEEDS_PUSH) {
        windowSize = std::max (
            (winWL.initSize ()),
            (THREAD_MULT_FACTOR * MIN_WIN_SIZE));

      } else {
        windowSize = std::min (
            (winWL.initSize () / INIT_MAX_ROUNDS),
            (THREAD_MULT_FACTOR * MIN_WIN_SIZE));
      }
    } else {

      assert (windowSize > 0);

      double commitRatio = double (currCommits) / double (prevWindowSize);

      if (commitRatio >= TARGET_COMMIT_RATIO) {
        windowSize *= 2;
        // windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 
        // windowSize = windowSize + windowSize / 2;

      } else {
        windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 

        // if (commitRatio / TARGET_COMMIT_RATIO < 0.90) {
          // windowSize = windowSize - (windowSize / 10);
// 
        // } else {
          // windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 
        // }
      }
    }

    if (windowSize < MIN_WIN_SIZE) { 
      windowSize = MIN_WIN_SIZE;
    }

    assert (windowSize > 0);


    if (NEEDS_PUSH) {
      if (winWL.empty () && (wl.size_all () > windowSize)) {
        // a case where winWL is empty and all the new elements were going into 
        // nextWL. When nextWL becomes bigger than windowSize, we must do something
        // to control efficiency. One solution is to spill all elements into winWL
        // and refill
        //

        spillAll (wl);

      } else if (wl.size_all () > (WIN_OVER_SIZE_FACTOR * windowSize)) {
        // too many adds. spill to control efficiency
        spillAll (wl);
      }
    }

    winWL.poll (wl, windowSize, wl.size_all (), ctxtMaker);
    // std::cout << "Calculated Window size: " << windowSize << ", Actual: " << wl->size_all () << std::endl;
  }


  GALOIS_ATTRIBUTE_PROF_NOINLINE void beginRound () {
    std::swap (currWL, nextWL);

    if (targetCommitRatio != 0.0) {
      size_t currCommits = roundCommits.reduceRO (); 

      size_t prevWindowSize = roundTasks.reduceRO ();
      refill (*currWL, currCommits, prevWindowSize);
    }

    roundCommits.reset ();
    roundTasks.reset ();
    nextWL->clear_all_parallel ();
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void endRound () {
    ++rounds;
    totalCommits += roundCommits.reduceRO ();
    totalTasks += roundTasks.reduceRO ();

    if (ENABLE_PARAMETER) {
      ParaMeter::StepStats s (rounds, roundCommits.reduceRO (), roundTasks.reduceRO ());
      s.dump (ParaMeter::getStatsFile (), loopname);
    }
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void expandNhoodImpl (HIDDEN::DummyExecFunc*) {
    // for stable case

    Galois::do_all_choice (makeLocalRange (*currWL),
        [this] (Ctxt* c) {
          UserCtxt& uhand = *userHandles.getLocal ();
          uhand.reset ();

          // nhFunc (c, uhand);
          runCatching (nhFunc, c, uhand);

          roundTasks += 1;
        },
        "expandNhood",
        chunk_size<NhFunc::CHUNK_SIZE> ());
  }

  struct GetActive: public std::unary_function<Ctxt*, const T&> {
    const T& operator () (const Ctxt* c) const {
      assert (c != nullptr);
      return c->getActive ();
    }
  };

  template <typename F>
  GALOIS_ATTRIBUTE_PROF_NOINLINE void expandNhoodImpl (F*) {
    // for unstable case
    auto m_beg = boost::make_transform_iterator (currWL->begin_all (), GetActive ());
    auto m_end = boost::make_transform_iterator (currWL->end_all (), GetActive ());

    Galois::do_all_choice (makeLocalRange (*currWL),
        [m_beg, m_end, this] (Ctxt* c) {
          UserCtxt& uhand = *userHandles.getLocal ();
          uhand.reset ();

          runCatching (nhFunc, c, uhand, m_beg, m_end);

          roundTasks += 1;
        },
        "expandNhoodUnstable",
        chunk_size<NhFunc::CHUNK_SIZE> ());
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void expandNhood () {
    // using ptr to execFunc to choose the right impl. 
    // relying on the fact that for stable case, the execFunc is DummyExecFunc. 
    expandNhoodImpl (&execFunc); 
  }

  inline void executeSourcesImpl (HIDDEN::DummyExecFunc*) {
  }

  template <typename F>
  GALOIS_ATTRIBUTE_PROF_NOINLINE void executeSourcesImpl (F*) {
    assert (HAS_EXEC_FUNC);

    Galois::do_all_choice (makeLocalRange (*currWL),
      [this] (Ctxt* ctxt) {

        UserCtxt& uhand = *userHandles.getLocal ();
        uhand.reset ();

        if (ctxt->isSrc ()) {
          execFunc (ctxt->getActive (), uhand);
        }
      },
      "exec-sources",
      Galois::chunk_size<ExFunc::CHUNK_SIZE> ());

  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void executeSources (void) {
    // using ptr to execFunc to choose the right impl. 
    // relying on the fact that for stable case, the execFunc is DummyExecFunc. 
    executeSourcesImpl (&execFunc);
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void applyOperator () {
    Galois::optional<T> minElem;

    if (NEEDS_PUSH) {
      if (targetCommitRatio != 0.0 && !winWL.empty ()) {
        minElem = *winWL.getMin();
      }
    }


    Galois::do_all_choice (makeLocalRange (*currWL),
        [this, &minElem] (Ctxt* c) {
          bool commit = false;

          UserCtxt& uhand = *userHandles.getLocal ();
          uhand.reset ();

          if (c->isSrc ()) {
            // opFunc (c->active, uhand);
            runCatching (opFunc, c, uhand);
            commit = c->isSrc (); // in case opFunc signalled abort
          } else {
            commit = false;
          }

          if (commit) {
            roundCommits += 1;
            if (NEEDS_PUSH) { 
              for (auto i = uhand.getPushBuffer ().begin ()
                  , endi = uhand.getPushBuffer ().end (); i != endi; ++i) {

                if ((targetCommitRatio == 0.0) || !minElem || !cmp (*minElem, *i)) {
                  // if *i >= *minElem
                  nextWL->push_back (ctxtMaker (*i));
                } else {
                  winWL.push (*i);
                } 
              }
            } else {
              assert (uhand.getPushBuffer ().begin () == uhand.getPushBuffer ().end ());
            }

            c->commitIteration ();
            c->~Ctxt ();
            ctxtAlloc.deallocate (c, 1);
          } else {
            c->cancelIteration ();
            c->reset ();
            nextWL->push_back (c);
          }
        },
        "applyOperator",
        chunk_size<OpFunc::CHUNK_SIZE> ());
  }


  void execute_impl () {

    while (true) {
      beginRound ();

      if (currWL->empty_all ()) {
        break;
      }

      Timer t;

      if (DETAILED_STATS) {
        std::printf ("trying to execute %zd elements\n", currWL->size_all ());
        t.start ();
      }

      expandNhood ();

      executeSources ();

      applyOperator ();

      endRound ();

      if (DETAILED_STATS) {
        t.stop ();
        std::printf ("Time taken: %ld\n", t.get ());
      }
      
    }
  }

  void printStats (void) {
    std::cout << "Two Phase Window executor, rounds: " << rounds << std::endl;
    std::cout << "Two Phase Window executor, commits: " << totalCommits << std::endl;
    std::cout << "Two Phase Window executor, total: " << totalTasks << std::endl;
    std::cout << "Two Phase Window executor, efficiency: " << double (totalCommits) / totalTasks << std::endl;
    std::cout << "Two Phase Window executor, avg. parallelism: " << double (totalCommits) / rounds << std::endl;
  }
  
};



/*
template <typename T, typename Cmp, typename NhFunc, typename OpFunc, typename ExFunc, typename WindowWL>

class KDGtwoPhaseUnstableExecutor: public KDGtwoPhaseStableExecutor<T, Cmp, NhFunc, OpFunc, WindowWL>  {
  using Base = KDGtwoPhaseStableExecutor<T, Cmp, NhFunc, OpFunc, WindowWL>;
  using Ctxt = typename Base::Ctxt;
  using CtxtWL = typename Base::CtxtWL;

  ExFunc execFunc;

public:
  KDGtwoPhaseUnstableExecutor (
      const Cmp& cmp, 
      const NhFunc& nhFunc,
      const OpFunc& opFunc,
      const ExFunc& execFunc)
    :
      Base (cmp, nhFunc, opFunc),
      execFunc (execFunc)
  {}


  void execute (void) {
    execute_unstable ();
  }

protected:
  struct GetActive: public std::unary_function<Ctxt*, const T&> {
    const T& operator () (const Ctxt* c) const {
      assert (c != nullptr);
      return c->getActive ();
    }
  };

  GALOIS_ATTRIBUTE_PROF_NOINLINE void expandNhoodUnstable (CtxtWL& currWL) {
    auto m_beg = boost::make_transform_iterator (currWL.begin_all (), GetActive ());
    auto m_end = boost::make_transform_iterator (currWL.end_all (), GetActive ());

    using UserCtxt = typename Base::UserCtxt;

    NhFunc& func = Base::nhFunc;
    typename Base::PerThreadUserCtxt& uh = Base::userHandles;
    GAccumulator<size_t>& total = Base::total;

    Galois::do_all_choice (makeLocalRange (currWL),
        [m_beg, m_end, &func, &uh, &total] (Ctxt* c) {
          UserCtxt& uhand = *uh.getLocal ();
          uhand.reset ();

          // nhFunc (c, uhand);
          // runCatching (nhFunc, c, uhand);
          Galois::Runtime::setThreadContext (c);
          int result = 0;
#ifdef GALOIS_USE_LONGJMP
          if ((result = setjmp(hackjmp)) == 0) {
#else
          try {
#endif 
            func (c->getActive (), uhand, m_beg, m_end);

#ifdef GALOIS_USE_LONGJMP
          } else {
            // nothing to do here
          }
#else
          } catch (ConflictFlag f) {
            result = f;
          }
#endif

          switch (result) {
            case 0:
              break;
            case CONFLICT: 
              c->disableSrc ();
              break;
            default:
              GALOIS_DIE ("can't handle conflict flag type");
              break;
          }
          

          Galois::Runtime::setThreadContext (NULL);

          total += 1;
        },
        "expandNhood",
        chunk_size<NhFunc::CHUNK_SIZE> ());

  }

  void execute_unstable (void) {

    while (true) {

      Base::prepareRound ();

      if (Base::currWL->empty_all ()) {
        break;
      }

      expandNhoodUnstable (*Base::currWL);


      Galois::do_all_choice (makeLocalRange (*Base::currWL),
          [this] (Ctxt* ctx) {
            if (ctx->isSrc ()) {
              execFunc (ctx->getActive ());
            }
          }, 
          "execute-safe-sources",
          chunk_size<NhFunc::CHUNK_SIZE> ());

      Base::applyOperator ();
      
    }

  }

};


namespace hidden {

struct DummyNhFunc {
  static const unsigned CHUNK_SIZE = 4;
  template <typename T, typename C>
  inline void operator () (const T&, const C&) {}
};

} // end namespace hidden


template <typename T, typename Cmp, typename SafetyPhase, typename OpFunc, typename ExFunc, typename WindowWL> 

class IKDGunstableCustomSafety: public KDGtwoPhaseStableExecutor<T, Cmp, hidden::DummyNhFunc, OpFunc, WindowWL> {

  using Base = KDGtwoPhaseStableExecutor<T, Cmp, hidden::DummyNhFunc, OpFunc, WindowWL>;
  using Ctxt = typename Base::Ctxt;
  using CtxtWL = typename Base::CtxtWL;


  SafetyPhase safetyPhase;
  ExFunc execFunc;

public:
  IKDGunstableCustomSafety (
      const Cmp& cmp, 
      const SafetyPhase& safetyPhase,
      const OpFunc& opFunc,
      const ExFunc& execFunc)
    :
      Base (cmp, hidden::DummyNhFunc (), opFunc),
      safetyPhase (safetyPhase),
      execFunc (execFunc)
  {}


  void execute (void) {
    execute_unstable ();
  }

private:

  void execute_unstable (void) {

    while (true) {

      Base::prepareRound ();

      if (Base::currWL->empty_all ()) {
        break;
      }

      safetyPhase (makeLocalRange (*Base::currWL), typename Ctxt::PtrComparator ());

      Galois::do_all_choice (makeLocalRange (*Base::currWL),
          [this] (Ctxt* ctx) {
            if (ctx->isSrc ()) {
              execFunc (ctx->getActive ());
            }
          }, 
          "execute-safe-sources",
          chunk_size<ExFunc::CHUNK_SIZE> ());

      Base::applyOperator ();

    } // end while

  }

};

*/

} // end anonymous namespace

template <typename R, typename Cmp, typename NhFunc, typename OpFunc, typename ExFunc>
void for_each_ordered_ikdg (const R& range, const Cmp& cmp, const NhFunc& nhFunc, 
    const ExFunc& execFunc,  const OpFunc& opFunc, const char* loopname) {

  using T = typename R::value_type;
  using WindowWL = PQbasedWindowWL<T, Cmp>;

  
  const bool NEEDS_PUSH = DEPRECATED::ForEachTraits<OpFunc>::NeedsPush;

  using WindowWL = typename std::conditional<NEEDS_PUSH, PQbasedWindowWL<T, Cmp>, SortedRangeWindowWL<T, Cmp> >::type;
  const bool HAS_EXEC_FUNC = !(std::is_same<ExFunc, HIDDEN::DummyExecFunc>::value);

  const bool ENABLE_PARAMETER = true;

  using Exec = IKDGtwoPhaseExecutor<T, Cmp, NhFunc, ExFunc, OpFunc, WindowWL, NEEDS_PUSH, HAS_EXEC_FUNC, ENABLE_PARAMETER>;
  
  Exec e (cmp, nhFunc, execFunc, opFunc, loopname);

  const bool wakeupThreadPool = true;

  if (wakeupThreadPool) {
    Substrate::getThreadPool().burnPower(Galois::getActiveThreads ());
  }

  e.push_initial (range);
  e.execute ();

  if (wakeupThreadPool) {
    Substrate::getThreadPool().beKind ();
  }

}


template <typename R, typename Cmp, typename NhFunc, typename OpFunc, typename ExFunc>
void for_each_ordered_ikdg (const R& range, const Cmp& cmp, const NhFunc& nhFunc, const OpFunc& opFunc, const char* loopname) {

  for_each_ordered_ikdg (range, cmp, nhFunc, HIDDEN::DummyExecFunc (), opFunc, loopname);
}

} // end namespace Runtime
} // end namespace Galois

#endif //  GALOIS_RUNTIME_KDG_TWO_PHASE_H
