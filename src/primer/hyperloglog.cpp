#include "primer/hyperloglog.h"
#include <sys/types.h>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include "common/util/hash_util.h"
#include "type/limits.h"

namespace bustub {

template <typename KeyType>
HyperLogLog<KeyType>::HyperLogLog(int16_t n_bits)
    : cardinality_(0), initial_bits_(std::max(static_cast<int16_t>(0), n_bits)), buckets_((1 << initial_bits_), 0) {}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  /** @TODO(student) Implement this function! */
  return std::bitset<BITSET_CAPACITY>(hash);
}

template <typename KeyType>
auto HyperLogLog<KeyType>::PositionOfLeftmostOne(const std::bitset<BITSET_CAPACITY> &bset) const -> uint64_t {
  /** @TODO(student) Implement this function! */
  uint64_t pos = BITSET_CAPACITY - initial_bits_ - 1;
  uint64_t count = 1;
  while (pos >= 0 and bset.test(pos) == 0) {
    --pos;
    ++count;
  }
  return count;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  hash_t hash = CalculateHash(val);

  std::bitset<BITSET_CAPACITY> bset = ComputeBinary(hash);

  size_t shift = BITSET_CAPACITY - initial_bits_;

  size_t index = shift != 64 ? (hash >> shift) : 0;

  buckets_[index] = std::max(buckets_[index], PositionOfLeftmostOne(bset));
}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  const size_t m = buckets_.size();

  double sum = 0;
  for (uint64_t val : buckets_) {
    sum += std::pow(2.0, -static_cast<double>(val));
  }

  cardinality_ = std::floor(CONSTANT * m * m / sum);
}
template class HyperLogLog<int64_t>;
template class HyperLogLog<std::string>;

}  // namespace bustub
