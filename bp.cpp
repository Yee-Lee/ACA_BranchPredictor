#include "bp.h"
#include <map>
#include <cassert>
#include <cstdio>
#include <memory>
#include <utility>
using namespace std;

#define BUBBLE 0x4033

#define BRANCH_PREDICTOR TSPredictor
#define WRAP_INC(a, b) a = a==b ? 0 : a+1

class TSPredictor : public BranchPredictor
{
    template<typename AddrType_, typename DataType_>
    class Cache
    {
      public:
        typedef AddrType_ AddrType;
        typedef DataType_ DataType;
        typedef pair<AddrType, DataType> EntryType;
        Cache(const int assoc, const int n_order):
            assoc_(assoc),
            n_order_(n_order),
            storage_(new EntryType[assoc<<n_order]) {}
        virtual ~Cache() {};

        EntryType* Search(const AddrType addr) {
          const AddrType idx = addr & ((1<<this->n_order_)-1);
          EntryType *target_set = &this->storage_[idx*this->assoc_];
          for (int i = 0; i < this->assoc_; ++i) {
            EntryType *cur = target_set+i;
            if (Match(addr, *cur)) {
              return cur;
            }
          }
          return nullptr;
        }
        pair<bool,EntryType*> FindInsert(const AddrType addr) {
          // return valid, position
          const AddrType idx = addr & ((1<<this->n_order_)-1);
          EntryType *target_set = &this->storage_[idx*this->assoc_];
          EntryType *invalid_entry = FindInvalid(target_set);
          if (invalid_entry != nullptr) {
            return make_pair(false, invalid_entry);
          }
          WRAP_INC(this->counter_, this->assoc_);
          return make_pair(true, target_set+this->counter_);
        }
      protected:
        virtual bool Match(const AddrType addr, const EntryType &e) = 0;
        virtual EntryType* FindInvalid(EntryType *target_set) = 0;
        const int assoc_, n_order_;
        int counter_ = 0;
        unique_ptr<EntryType[]> storage_;
    };

    class BranchTargetCache: public Cache<uint32_t, uint32_t> {
      public:
        BranchTargetCache(int assoc, int n_order):
          Cache<uint32_t, uint32_t>(assoc, n_order)
        {
          for (int i = 0; i < this->assoc_<<this->n_order_; ++i) {
            this->storage_[i].first = 0;
          }
        }
      protected:
        virtual bool Match(const AddrType addr, const EntryType &e) {
          return addr == e.first;
        }
        virtual EntryType* FindInvalid(EntryType *target_set) {
          for (int i = 0; i < this->assoc_; ++i) {
            if (target_set[i].first == 0) {
              return target_set+i;
            }
          }
          return nullptr;
        }
    };

    class TsReplayHeadCache: public Cache<uint64_t, uint16_t> {
      public:
        TsReplayHeadCache(int assoc, int n_order, DataType timeout):
          Cache<uint64_t, uint16_t>(assoc, n_order),
          timestamp_(timeout), timeout_(timeout)
        {
          for (int i = 0; i < this->assoc_<<this->n_order_; ++i) {
            this->storage_[i].second = 0;
          }
        }
        DataType timestamp_;
      protected:
        DataType timeout_;
        virtual bool Match(const AddrType addr, const EntryType &e) {
          return
            e.first == addr and
            e.second+this->timeout_ > this->timestamp_ and
            e.second > this->timestamp_-this->timeout_;
        }
        virtual EntryType* FindInvalid(EntryType *target_set) {
          for (int i = 0; i < this->assoc_; ++i) {
            if (
              target_set[i].second+this->timeout_ > this->timestamp_ and
              target_set[i].second > this->timestamp_-this->timeout_
            ) {
              return target_set+i;
            }
          }
          return nullptr;
        }
    };
  private:
    uint32_t hash_with_history(const uint32_t pc) {
      // Use 34 bit of history and 30 bit of pc.
      // It's just because that it fit uint64_t.
      return (this->history_<<30)|(pc>>2);
    }
    uint64_t history_ = 0;

    /* Use TS history of TS_LEN
     * In tsc, we use a timestamp to represent the replaying head.
     * We can compare the current timestamp and the recorded timestamp to check whether it's valid.
     * Currently we just accept timestamp overflow.
     */
    static constexpr uint16_t TS_LEN = 1024; // must divide 65536
    uint16_t replay_position_ = 0;
    bool ts_history_[TS_LEN];
    bool base_prediction_ = false, replaying_ = false;
    TsReplayHeadCache tsc_;
    BranchTargetCache btc_;
  public:
    TSPredictor ( struct bp_io& io ) : BranchPredictor ( io ), tsc_(4, 10, TS_LEN), btc_(4, 10)
    {
    }

    ~TSPredictor ( )
    {
    }

    uint32_t predict_fetch ( uint32_t pc )
    {
      this->base_prediction_ = true; // TODO: predict use BP
      bool ts_prediction = this->base_prediction_;
      if (this->replaying_) {
        if (this->ts_history_[this->replay_position_]) {
          ts_prediction = not ts_prediction;
        }
        WRAP_INC(this->replay_position_, TS_LEN);
      }
      if (ts_prediction) {
        // Lookup branch target buffer
        BranchTargetCache::EntryType *entry = this->btc_.Search(pc);
        return entry == nullptr ? 0 : entry->second;
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
      // update branch target cache
      if (outcome) {
        BranchTargetCache::EntryType *entry = this->btc_.Search(pc);
        if (entry == nullptr) {
          // Not found
          entry = this->btc_.FindInsert(pc).second;
          entry->first = pc;
        }
        // Store pc -> pc_next mapping
        assert(entry->first == pc);
        entry->second = pc_next;
      }
      const bool base_is_correct = this->base_prediction_ == outcome;
      this->history_ = (this->history_<<1) | outcome;
      this->ts_history_[this->tsc_.timestamp_%TS_LEN] = base_is_correct;
      this->tsc_.timestamp_++;
      if (mispredict) {
        this->replaying_ = false;
      }
      if (not base_is_correct) {
        TsReplayHeadCache::AddrType hashed = hash_with_history(pc);
        TsReplayHeadCache::EntryType *entry = this->tsc_.Search(hashed);
        if (entry != nullptr) {
          // Found
          this->replay_position_ = entry->second;
          this->replaying_ = true;
        } else {
          // Not found
          entry = this->tsc_.FindInsert(hashed).second;
          entry->first = hashed;
        }
        // Store (pc, history) -> timestamp mapping
        assert(entry->first == hashed);
        entry->second = this->tsc_.timestamp_;
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

