#include "primer/hyperloglog_presto.h"
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "common/enums/statement_type.h"
#include "common/util/hash_util.h"

namespace bustub {

template <typename KeyType>
HyperLogLogPresto<KeyType>::HyperLogLogPresto(int16_t n_leading_bits)
    : cardinality_(0),
      leading_bits_(std::max(static_cast<int16_t>(0), n_leading_bits)),
      dense_bucket_(1 << leading_bits_, 0) {}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::CountRightmostZeros(std::bitset<sizeof(hash_t) * 8> &bset) -> uint8_t {
  size_t i = 0, len = bset.size() - leading_bits_;
  while (i < len and bset.test(i) == 0) {
    ++i;
  }
  return i;
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::combine(size_t index) -> uint64_t {
  uint64_t low = dense_bucket_.at(index).to_ulong();
  uint64_t high = 0UL;
  if (overflow_bucket_.find(index) != overflow_bucket_.end()) {
    high = overflow_bucket_[index].to_ulong();
  }
  return low | (high << 4);
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::storeInContainer(uint8_t val, size_t index) -> void {
  if (static_cast<uint8_t>(combine(index)) < val) {
    uint8_t low = val & 0x0f;
    uint8_t high = ((val & 0xf0) >> 4);
    std::bitset<DENSE_BUCKET_SIZE> lowSet(low);
    std::bitset<OVERFLOW_BUCKET_SIZE> highSet(high);
    dense_bucket_[index] = lowSet;
    overflow_bucket_[index] = highSet;
  }
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  hash_t hash = CalculateHash(val);

  std::bitset<sizeof(hash_t) * 8> bset(hash);

  uint8_t count = CountRightmostZeros(bset);

  uint64_t shift = sizeof(hash) * 8 - leading_bits_;

  size_t index = shift != 64 ? (hash >> shift) : 0;

  storeInContainer(count, index);
}

template <typename T>
auto HyperLogLogPresto<T>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  const size_t m = dense_bucket_.size();

  double sum = 0;
  for (size_t i = 0; i < m; ++i) {
    uint64_t val = combine(i);
    sum += std::pow(2.0, -static_cast<double>(val));
  }

  cardinality_ = std::floor(CONSTANT * m * m / sum);
}

template class HyperLogLogPresto<int64_t>;
template class HyperLogLogPresto<std::string>;
}  // namespace bustub
