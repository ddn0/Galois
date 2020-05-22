#pragma once

#include <cstdint>

////////////////////////////////////////////////////////////////////////////////
// Field flag class
////////////////////////////////////////////////////////////////////////////////

namespace galois {
namespace runtime {

/**
 * Bitvector status enum specifying validness of certain things in bitvector.
 */
enum BITVECTOR_STATUS {
  NONE_INVALID, //!< none of the bitvector is invalid
  SRC_INVALID,  //!< sources on bitvector are invalid
  DST_INVALID,  //!< destinations on bitvector are invalid
  BOTH_INVALID  //< both source and destinations on bitvector are invalid
};

//! Return true if the sources are invalid in bitvector flag
bool src_invalid(BITVECTOR_STATUS bv_flag);
//! Return true if the destinations are invalid in bitvector flag
bool dst_invalid(BITVECTOR_STATUS bv_flag);
//! Marks sources invalid on passed in bitvector flag
void make_src_invalid(BITVECTOR_STATUS* bv_flag);
//! Marks destinations invalid on passed in bitvector flag
void make_dst_invalid(BITVECTOR_STATUS* bv_flag);

/**
 * Each field has a FieldFlags object that indicates synchronization status
 * of that field.
 */
class FieldFlags {
private:
  uint8_t _s2s;
  uint8_t _s2d;
  uint8_t _d2s;
  uint8_t _d2d;

public:
  /**
   * Status of the bitvector in terms of if it can be used to sync the field
   */
  BITVECTOR_STATUS bitvectorStatus;
  /**
   * Field Flags constructor. Sets all flags to false and bitvector
   * status to invalid.
   */
  FieldFlags() {
    _s2s            = false;
    _s2d            = false;
    _d2s            = false;
    _d2d            = false;
    bitvectorStatus = BITVECTOR_STATUS::NONE_INVALID;
  }

  //! Return true if src2src is set
  bool src_to_src() const { return _s2s; }

  //! Return true if src2dst is set
  bool src_to_dst() const { return _s2d; }

  //! Return true if dst2src is set
  bool dst_to_src() const { return _d2s; }

  //! Return true if dst2dst is set
  bool dst_to_dst() const { return _d2d; }

  //! Sets write src flags to true
  void set_write_src() {
    _s2s = true;
    _s2d = true;
  }

  //! Sets write dst flags to true
  void set_write_dst() {
    _d2s = true;
    _d2d = true;
  }

  //! Sets all write flags to true
  void set_write_any() {
    _s2s = true;
    _s2d = true;
    _d2s = true;
    _d2d = true;
  }

  //! Sets write src flags to false
  void clear_read_src() {
    _s2s = false;
    _d2s = false;
  }

  //! Sets write dst flags to false
  void clear_read_dst() {
    _s2d = false;
    _d2d = false;
  }

  //! Sets all write flags to false
  void clear_read_any() {
    _s2d = false;
    _d2d = false;
    _s2s = false;
    _d2s = false;
  }

  //! Sets all write flags to false and sets bitvector stats to none invalid
  void clear_all() {
    _s2s            = false;
    _s2d            = false;
    _d2s            = false;
    _d2d            = false;
    bitvectorStatus = BITVECTOR_STATUS::NONE_INVALID;
  }
};

} // end namespace runtime
} // end namespace galois
