#include "bp.h"
#include <map>
#include <cassert>
#include <cstdio>
#include <memory>
#include <utility>
using namespace std;

#define BUBBLE 0x4033

#define BRANCH_PREDICTOR TSPredictor

class TSPredictor : public BranchPredictor
{
    template<typename AddrType, typename DataType>
    class Cache
    {
      public:
        typedef pair<AddrType, DataType> EntryType;
        Cache(const int assoc, const int n_order):
            assoc_(assoc),
            n_order_(n_order),
            storage_(new EntryType[assoc<<n_order]) {}
        virtual ~Cache() {};

        EntryType* Search(const AddrType addr) {
          const AddrType idx = addr >> this->n_order_;
          EntryType *target_set = &(this->storage_[idx*assoc_+i]);
          for (int i = 0; i < this->assoc_; ++i) {
            EntryType *cur = target_set+i;
            if (Match(cur, addr)) {
              return cur;
            }
          }
          return nullptr;
        }
        pair<bool,EntryType*> FindInsert(const AddrType addr) {
          // return valid, position
          const AddrType idx = addr >> this->n_order_;
          EntryType *target_set = &(this->storage_[idx*assoc_+i]);
          EntryType *invalid_entry = FindInvalid(target_set);
          if (invalid_entry == nullptr) {
            return make_pair(false, invalid_entry);
          }
          this->counter = this->counter == this->assoc_ ? 0 : this->counter_+1;
          return make_pair(true, target_set+this->counter_);
        }
      protected:
        virtual bool Match(const AddrType addr, const EntryType e) = 0;
        virtual EntryType* FindInvalid(const EntryType *target_set) = 0;
      private:
        int assoc_, n_order_, counter_ = 0;
        unique_ptr<EntryType[]> storage_;
    };

    class TsReplayHeadCache: public Cache<uint64_t, uint16_t> {
      public:
        TsReplayHeadCache(int assoc, int n_order, DataType timeout):
            Cache<uint64_t, uint16_t>(assoc, n_order),
            timestamp_(timeout), timeout_(timeout) {}
          for (int i = 0; i < this->assoc<<this->n_order_; ++i) {
            this->storage_[i].second = 0;
          }
        }
        DataType timestamp_;
      protected:
        DataType timeout_;
        virtual bool Match(const AddrType addr, const EntryType e) {
          return e.first == addr and e.second+timeout>timestamp_ and e.second>timestamp_-timeout;
        }
        virtual EntryType* FindInvalid(const EntryType *target_set) {
          for (int i = 0; i < this->assoc_; ++i) {
            if (target_set[i].second+timeout>timestamp_ and target_set[i].second>timestamp_-timeout) {
              return target_set[i];
            }
          }
          return nullptr;
        }
    };
  private:
    uint32_t hash_with_history(const uint32_t pc) {
      // Use 34 bit of history and 30 bit of pc.
      // It's just because that it fit uint64_t.
      return (history<<30)|(pc>>2);
    }
    uint64_t history = 0;

    /* Use TS history of TS_LEN
     * In tsc, we use a timestamp to represent the replaying head.
     * We can compare the current timestamp and the recorded timestamp to check whether it's valid.
     * Currently we just accept timestamp overflow.
     */
    const uint16_t TS_LEN = 1024; // must divide 65536
    uint16_t replay_position = 0;
    bool ts_history[TS_LEN];
    bool base_prediction = false, replaying = false;
    TsReplayHeadCache tsc;
  public:
    TSPredictor ( struct bp_io& io ) : BranchPredictor ( io ), tsc(TS_LEN)
    {
    }

    ~TSPredictor ( )
    {
    }

    uint32_t predict_fetch ( uint32_t pc )
    {
      base_prediction = /* TODO: predict use BP */;
      bool ts_prediction = base_prediction;
      if (replaying) {
        if (ts_history[replay_position]) {
          ts_prediction = not ts_prediction;
        }
        replay_position = replay_position==TS_LEN-1 ? 0: replay_position+1;
      }
      if (ts_prediction) {
        // TODO: lookup branch target buffer
      } else {
        return 0;
      }
    }

    void update_execute ( uint32_t pc,
                          uint32_t pc_next,
                          bool mispredict,
                          bool is_brjmp,
                          uint32_t inst )
    {
      if (not is_brjmp) {
        return;
      }
      // TODO: update BP
      const bool outcome = pc+4 != pc_next;
      const bool base_is_correct = base_prediction == outcome;
      history = (history<<1) | outcome;
      ts_history[tsc.timestamp_%TS_LEN] = base_prediction == outcome;
      tsc.timestamp_++;
      if (mispredict) {
        replaying = false;
      }
      if (not base_is_correct) {
        TsReplayHeadCache::AddrType hashed = hash_with_history(pc);
        TsReplayHeadCache::EntryType *entry = tsc.Search(hashed);
        if (entry != nullptr) {
          // Found
          replay_position = entry->second;
          replaying = true;
        } else {
          // Not found
          entry = tsc.Insert(hashed).second;
          entry->first = tsc.hashed;
        }
        // Store (pc, history) -> timestamp mapping
        assert(entry->first == hashed);
        entry->second = tsc.timestamp_;
      }
    }
};


//
// Baseline Branch Predictor: simple BTB
//

#define BTB_ADDR_BITS 5
#define BTB_ENTRIES (1 << (BTB_ADDR_BITS-1))

// Sample branch predictor provided for you: a simple branch target buffer.
class BTB : public BranchPredictor 
{
  private:
    typedef struct {
      uint32_t target_pc;
      uint32_t tag_pc;
    }BTBEntry_t;
    BTBEntry_t* table;

  public:


    BTB ( struct bp_io& _io ) : BranchPredictor ( _io )
    {
      table = new BTBEntry_t[BTB_ENTRIES];
      memset ( table, 0, sizeof(BTBEntry_t) * BTB_ENTRIES );
    }

    ~BTB ( )
    {
      delete[] table;
    }
    
    // Given a PC, figure out which row of the BTB table to examine
    inline uint32_t index ( const uint32_t pc )
    {
      // Extract lower BTB_ADDR_BTS bits from (pc >> 2)
      // Shift PC right two because lower two bits always zero.
      const uint32_t mask = (1 << (BTB_ADDR_BITS-1)) - 1;
      return (pc >> 2) & mask;
    }

    uint32_t predict_fetch ( uint32_t pc )
    {
      BTBEntry_t entry = table[index ( pc )];

      // Only return a prediction of the entry's tag matches this
      // PC in order to avoid aliasing.

      if ( entry.tag_pc == pc ) 
        return entry.target_pc;

      return 0;
    }

    void update_execute ( uint32_t pc, 
                          uint32_t pc_next, 
                          bool mispredict,
                          bool is_brjmp, 
                          uint32_t inst )
    {
      if( inst == BUBBLE )
        return;

      uint32_t btb_index = index ( pc );
      BTBEntry_t new_entry, 
                       old_entry = table[btb_index];

      new_entry.target_pc = pc_next;
      new_entry.tag_pc = pc;

      if ( is_brjmp )
        table[btb_index] = new_entry;
    }
};


//
// A fully-associative, infinitely large BTB.  This code demonstrates how
// to use the STL map container as a fully-associative table.
//
class InfiniteBTB : public BranchPredictor
{
  private:
    // Map PC to Predicted Target or zero if none (PC+4)
    std::map<uint32_t, uint32_t> table;
  public:
    InfiniteBTB ( struct bp_io& _io ) : BranchPredictor ( _io ) { }
    ~InfiniteBTB ( ) { }

    uint32_t predict_fetch ( uint32_t pc )
    {
      if ( table.find( pc ) != table.end() ) // Does table contain pc?
        return table[pc];  
      return 0;
    }
    
    void update_execute ( 
        uint32_t pc, 
        uint32_t pc_next, 
        bool mispredict,
        bool is_brjmp, 
        uint32_t inst )
    {
      if ( is_brjmp )
        table[pc] = pc_next;
    }
};

//
// No Branch Predictrion: always predict "not taken"
//
class NoBP: public BranchPredictor
{
  public:
    NoBP ( struct bp_io& _io ) : BranchPredictor ( _io ) 
    {
    }

    ~NoBP ( ) 
    { 
    }

    uint32_t predict_fetch ( uint32_t pc )
    {
      return 0;
    }
    
    void update_execute ( 
        uint32_t pc, 
        uint32_t pc_next, 
        bool mispredict,
        bool is_brjmp, 
        uint32_t inst )
    {
    }
};




// 
// Set control signals to emulator
// (You should probably ignore this code)
//

void BranchPredictor::clock_lo ( dat_t<1> reset )
{

  // if( reset.lo_word ( ) || !io.stats_reg_ptr->lo_word ( ) )
  //   return;

   if( reset.lo_word ( ))
     return;

  
  // Examine instruction in execute stage and use it to call update_execute()
  update_execute_base (
      io.exe_reg_pc_ptr->lo_word ( ),
      io.exe_pc_next_ptr->lo_word ( ),
      io.exe_mispredict_ptr->lo_word ( ),
      // BR_N (no branch/jump) = 0 (see consts.scala)
      io.exe_br_type_ptr->lo_word ( ) != 0, 
      io.exe_reg_inst_ptr->lo_word ( ) );
}



void BranchPredictor::clock_hi ( dat_t<1> reset ) 
{
  if ( reset.lo_word ( ) ) 
    return;

  // Extract PC of instruction being fetched, and call predict_fetch with it,
  // and use the prediction to set relevant control signals back in the 
  // processor.
  uint32_t if_pc        = io.if_pc_reg_ptr->lo_word();
  uint32_t if_pred_targ = predict_fetch ( if_pc );
   *(io.if_pred_target_ptr) = LIT<32>( if_pred_targ );
   *(io.if_pred_taken_ptr) = LIT<1>( if_pred_targ != 0 );

}


void BranchPredictor::update_execute_base ( 
                          uint32_t pc,        // PC of this inst (in execute)
                          uint32_t pc_next,   // actual next PC of this inst
                          bool mispredict,    // Did we mispredict?
                          bool     is_brjmp,  // is actually a branch or jump
                          uint32_t inst )     // The inst itself, in case you
{                                             // want to extract arbitrary info
  if ( inst != BUBBLE ) 
  {
    brjmp_count += 1 & (long) is_brjmp;
    inst_count++;
    mispred_count += 1 & (long) ( mispredict );
  }
  cycle_count++;
  update_execute ( pc, pc_next, mispredict, is_brjmp, inst );
}


BranchPredictor::BranchPredictor ( struct bp_io& _io )  : io(_io) 
{
  brjmp_count   = 0;
  mispred_count = 0;
  inst_count    = 0;
  cycle_count   = 0;
}


BranchPredictor::~BranchPredictor ( )
{
  
  fprintf ( stderr, "##--------- PROFILING --------------------\n");
  fprintf ( stderr, "## INSTS  %ld\n", inst_count );
  fprintf ( stderr, "## CYCLES %ld\n", cycle_count );
  fprintf ( stderr, "## IPC    %f\n", (double)inst_count/cycle_count );
  fprintf ( stderr, "\n");  
  fprintf ( stderr, "## BRJMPs      %ld\n", brjmp_count );
  fprintf ( stderr, "## MISPREDICTS %ld\n", mispred_count );
  fprintf ( stderr, "## MPKI        %f\n", ((double)(mispred_count*1000)/inst_count) );
  fprintf ( stderr, "## MISS RATE   %f\n", ((double)mispred_count/brjmp_count) );

}


BranchPredictor* BranchPredictor::make_branch_predictor ( struct bp_io& io )
{
  return new BRANCH_PREDICTOR ( io );
}

