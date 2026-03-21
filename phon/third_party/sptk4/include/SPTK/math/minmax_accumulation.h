// ------------------------------------------------------------------------ //
// Copyright 2021 SPTK Working Group                                        //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ------------------------------------------------------------------------ //

#ifndef SPTK_MATH_MINMAX_ACCUMULATION_H_
#define SPTK_MATH_MINMAX_ACCUMULATION_H_

#include <functional>  // std::greater, std::less
#include <map>         // std::multimap

#include "SPTK/utils/sptk_utils.h"

namespace sptk {

/**
 * Compute minimum and maximum given data sequence.
 */
class MinMaxAccumulation {
 public:
  /**
   * Buffer for MinMaxAccumulation.
   */
  class Buffer {
   public:
    Buffer() : position_(0) {
    }

    virtual ~Buffer() {
    }

   private:
    void Clear() {
      position_ = 0;
      minimum_.clear();
      maximum_.clear();
    }

    int position_;
    std::multimap<double, int, std::greater<double> > minimum_;
    std::multimap<double, int, std::less<double> > maximum_;

    friend class MinMaxAccumulation;
    DISALLOW_COPY_AND_ASSIGN(Buffer);
  };

  /**
   * @param[in] num_best Number of minimum/maximum numbers.
   */
  explicit MinMaxAccumulation(int num_best);

  virtual ~MinMaxAccumulation() {
  }

  /**
   * @return Number of minimum/maximum numbers.
   */
  int GetNumBest() const {
    return num_best_;
  }

  /**
   * @return True if this object is valid.
   */
  bool IsValid() const {
    return is_valid_;
  }

  /**
   * Get @f$n@f$-th minimum value and its position.
   *
   * @param[in] rank Rank @f$n@f$.
   * @param[in] buffer Buffer.
   * @param[out] value Minimum value.
   * @param[out] position Position of the minimum value.
   * @return True on success, false on failure.
   */
  bool GetMinimum(int rank, const MinMaxAccumulation::Buffer& buffer,
                  double* value, int* position) const;

  /**
   * Get @f$n@f$-th maximum value and its position.
   *
   * @param[in] rank Rank @f$n@f$.
   * @param[in] buffer Buffer.
   * @param[out] value Maximum value.
   * @param[out] position Position of the maximum value.
   * @return True on success, false on failure.
   */
  bool GetMaximum(int rank, const MinMaxAccumulation::Buffer& buffer,
                  double* value, int* position) const;

  /**
   * Clear buffer.
   *
   * @param[out] buffer Buffer.
   */
  void Clear(MinMaxAccumulation::Buffer* buffer) const;

  /**
   * Accumulate minimum and maximum.
   *
   * @param[in] data Input data.
   * @param[in,out] buffer Buffer.
   * @return True on success, false on failure.
   */
  bool Run(double data, MinMaxAccumulation::Buffer* buffer) const;

 private:
  const int num_best_;

  bool is_valid_;

  DISALLOW_COPY_AND_ASSIGN(MinMaxAccumulation);
};

}  // namespace sptk

#endif  // SPTK_MATH_MINMAX_ACCUMULATION_H_
