/*!
 * Copyright (c) 2020 Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See LICENSE file in the project root for license information.
 */
#ifndef LIGHTGBM_IO_MULTI_VAL_DENSE_BIN_HPP_
#define LIGHTGBM_IO_MULTI_VAL_DENSE_BIN_HPP_


#include <LightGBM/bin.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace LightGBM {

template <typename VAL_T>
class MultiValDenseBin;

template <typename VAL_T>
class MultiValDenseBinIterator : public BinIterator {
public:
  explicit MultiValDenseBinIterator(const MultiValDenseBin<VAL_T>* bin_data, uint32_t min_bin, uint32_t max_bin, uint32_t most_freq_bin)
    : bin_data_(bin_data), min_bin_(static_cast<VAL_T>(min_bin)),
    max_bin_(static_cast<VAL_T>(max_bin)),
    most_freq_bin_(static_cast<VAL_T>(most_freq_bin)) {
    if (most_freq_bin_ == 0) {
      offset_ = 1;
    } else {
      offset_ = 0;
    }
  }
  inline uint32_t Get(data_size_t idx) override;
  inline uint32_t RawGet(data_size_t idx) override {
    Log::Fatal("No RawGet for MultiValDenseBinIterator");
    return 0;
  }
  inline void Reset(data_size_t) override {}

private:
  const MultiValDenseBin<VAL_T>* bin_data_;
  VAL_T min_bin_;
  VAL_T max_bin_;
  VAL_T most_freq_bin_;
  uint8_t offset_;
};
/*!
* \brief Used to store bins for dense feature
* Use template to reduce memory cost
*/
template <typename VAL_T>
class MultiValDenseBin : public Bin {
public:
  friend MultiValDenseBinIterator<VAL_T>;
  explicit MultiValDenseBin(data_size_t num_data)
    : num_data_(num_data) {
    push_buf_.resize(num_data_);
  }

  ~MultiValDenseBin() {
  }

  void Push(int, data_size_t idx, uint32_t value) override {
    push_buf_[idx].push_back(static_cast<VAL_T>(value));
  }

  void ReSize(data_size_t num_data) override {
    if (num_data_ != num_data) {
      num_data_ = num_data;
      row_ptr_.resize(num_data_ + 1);
    }
  }

  BinIterator* GetIterator(uint32_t min_bin, uint32_t max_bin, uint32_t default_bin) const override;

  #define ACC_GH(hist, i, g, h) \
  const auto ti = static_cast<int>(i) << 1; \
  hist[ti] += g; \
  hist[ti + 1] += h; \

  void ConstructHistogram(const data_size_t* data_indices, data_size_t start, data_size_t end,
    const score_t* ordered_gradients, const score_t* ordered_hessians,
    hist_t* out) const override {
    const data_size_t prefetch_size = 16;
    for (data_size_t i = start; i < end; ++i) {
      if (prefetch_size + i < end) {
        PREFETCH_T0(row_ptr_.data() + data_indices[i + prefetch_size]);
        PREFETCH_T0(ordered_gradients + i + prefetch_size);
        PREFETCH_T0(ordered_hessians + i + prefetch_size);
        PREFETCH_T0(data_.data() + row_ptr_[data_indices[i + prefetch_size]]);
      }
      for (data_size_t idx = RowPtr(data_indices[i]); idx < RowPtr(data_indices[i] + 1); ++idx) {
        const VAL_T bin = data_[idx];
        ACC_GH(out, bin, ordered_gradients[i], ordered_hessians[i]);
      }
    }
  }

  void ConstructHistogram(data_size_t start, data_size_t end,
    const score_t* ordered_gradients, const score_t* ordered_hessians,
    hist_t* out) const override {
    const data_size_t prefetch_size = 16;
    for (data_size_t i = start; i < end; ++i) {
      if (prefetch_size + i < end) {
        PREFETCH_T0(row_ptr_.data() + i + prefetch_size);
        PREFETCH_T0(ordered_gradients + i + prefetch_size);
        PREFETCH_T0(ordered_hessians + i + prefetch_size);
        PREFETCH_T0(data_.data() + row_ptr_[i + prefetch_size]);
      }
      for (data_size_t idx = RowPtr(i); idx < RowPtr(i + 1); ++idx) {
        const VAL_T bin = data_[idx];
        ACC_GH(out, bin, ordered_gradients[i], ordered_hessians[i]);
      }
    }
  }

  void ConstructHistogram(const data_size_t* data_indices, data_size_t start, data_size_t end,
    const score_t* ordered_gradients,
    hist_t* out) const override {
    const data_size_t prefetch_size = 16;
    for (data_size_t i = start; i < end; ++i) {
      if (prefetch_size + i < end) {
        PREFETCH_T0(row_ptr_.data() + data_indices[i + prefetch_size]);
        PREFETCH_T0(ordered_gradients + i + prefetch_size);
        PREFETCH_T0(data_.data() +  row_ptr_[data_indices[i + prefetch_size]]);
      }
      for (data_size_t idx = RowPtr(data_indices[i]); idx < RowPtr(data_indices[i] + 1); ++idx) {
        const VAL_T bin = data_[idx];
        ACC_GH(out, bin, ordered_gradients[i], 1.0f);
      }
    }
  }

  void ConstructHistogram(data_size_t start, data_size_t end,
    const score_t* ordered_gradients,
    hist_t* out) const override {
    const data_size_t prefetch_size = 16;
    for (data_size_t i = start; i < end; ++i) {
      if (prefetch_size + i < end) {
        PREFETCH_T0(row_ptr_.data() + i + prefetch_size);
        PREFETCH_T0(ordered_gradients + i + prefetch_size);
        PREFETCH_T0(data_.data() + row_ptr_[i + prefetch_size]);
      }
      for (data_size_t idx = RowPtr(i); idx < RowPtr(i + 1); ++idx) {
        const VAL_T bin = data_[idx];
        ACC_GH(out, bin, ordered_gradients[i], 1.0f);
      }
    }
  }
  #undef ACC_GH

  data_size_t Split(
    uint32_t min_bin, uint32_t max_bin, uint32_t default_bin, uint32_t most_freq_bin, MissingType missing_type, bool default_left,
    uint32_t threshold, data_size_t* data_indices, data_size_t num_data,
    data_size_t* lte_indices, data_size_t* gt_indices) const override {
    if (num_data <= 0) { return 0; }
    VAL_T th = static_cast<VAL_T>(threshold + min_bin);
    const VAL_T minb = static_cast<VAL_T>(min_bin);
    const VAL_T maxb = static_cast<VAL_T>(max_bin);
    VAL_T t_default_bin = static_cast<VAL_T>(min_bin + default_bin);
    VAL_T t_most_freq_bin = static_cast<VAL_T>(min_bin + most_freq_bin);
    if (most_freq_bin == 0) {
      th -= 1;
      t_default_bin -= 1;
      t_most_freq_bin -= 1;
    }
    data_size_t lte_count = 0;
    data_size_t gt_count = 0;
    data_size_t* default_indices = gt_indices;
    data_size_t* default_count = &gt_count;
    data_size_t* missing_default_indices = gt_indices;
    data_size_t* missing_default_count = &gt_count;
    if (most_freq_bin <= threshold) {
      default_indices = lte_indices;
      default_count = &lte_count;
    }
    if (missing_type == MissingType::NaN) {
      if (default_left) {
        missing_default_indices = lte_indices;
        missing_default_count = &lte_count;
      }
      for (data_size_t i = 0; i < num_data; ++i) {
        const data_size_t idx = data_indices[i];
        VAL_T bin = GetRawBin(idx, minb, maxb, t_most_freq_bin);
        if (bin == maxb) {
          missing_default_indices[(*missing_default_count)++] = idx;
        } else if (t_most_freq_bin == bin) {
          default_indices[(*default_count)++] = idx;
        } else if (bin > th) {
          gt_indices[gt_count++] = idx;
        } else {
          lte_indices[lte_count++] = idx;
        }
      }
    } else {
      if ((default_left && missing_type == MissingType::Zero)
        || (default_bin <= threshold && missing_type != MissingType::Zero)) {
        missing_default_indices = lte_indices;
        missing_default_count = &lte_count;
      }
      if (default_bin == most_freq_bin) {
        for (data_size_t i = 0; i < num_data; ++i) {
          const data_size_t idx = data_indices[i];
          VAL_T bin = GetRawBin(idx, minb, maxb, t_most_freq_bin);
          if (t_most_freq_bin == bin) {
            missing_default_indices[(*missing_default_count)++] = idx;
          } else if (bin > th) {
            gt_indices[gt_count++] = idx;
          } else {
            lte_indices[lte_count++] = idx;
          }
        }
      } else {
        for (data_size_t i = 0; i < num_data; ++i) {
          const data_size_t idx = data_indices[i];
          VAL_T bin = GetRawBin(idx, minb, maxb, t_most_freq_bin);
          if (bin == t_default_bin) {
            missing_default_indices[(*missing_default_count)++] = idx;
          } else if (t_most_freq_bin == bin) {
            default_indices[(*default_count)++] = idx;
          } else if (bin > th) {
            gt_indices[gt_count++] = idx;
          } else {
            lte_indices[lte_count++] = idx;
          }
        }
      }
    }
    return lte_count;
  }

  data_size_t SplitCategorical(
    uint32_t min_bin, uint32_t max_bin, uint32_t most_freq_bin,
    const uint32_t* threshold, int num_threahold, data_size_t* data_indices, data_size_t num_data,
    data_size_t* lte_indices, data_size_t* gt_indices) const override {
    if (num_data <= 0) { return 0; }
    data_size_t lte_count = 0;
    data_size_t gt_count = 0;
    data_size_t* default_indices = gt_indices;
    data_size_t* default_count = &gt_count;
    if (Common::FindInBitset(threshold, num_threahold, most_freq_bin)) {
      default_indices = lte_indices;
      default_count = &lte_count;
    }
    for (data_size_t i = 0; i < num_data; ++i) {
      const data_size_t idx = data_indices[i];
      uint32_t bin = GetRawBin(idx, min_bin, max_bin, most_freq_bin);
      if (bin == most_freq_bin) {
        default_indices[(*default_count)++] = idx;
      } else if (Common::FindInBitset(threshold, num_threahold, bin - min_bin)) {
        lte_indices[lte_count++] = idx;
      } else {
        gt_indices[gt_count++] = idx;
      }
    }
    return lte_count;
  }

  data_size_t num_data() const override { return num_data_; }

  void FinishLoad() override {
    data_.clear();
    row_ptr_.resize(num_data_ + 1, 0);
    data_size_t cur_pos = 0;
    for (data_size_t i = 0; i < num_data_; ++i) {
      data_size_t cnt_feat = static_cast<data_size_t>(push_buf_[i].size());
      std::sort(push_buf_[i].begin(), push_buf_[i].end());
      for (data_size_t j = 0; j < cnt_feat; ++j) {
        data_.push_back(push_buf_[i][j]);
      }
      push_buf_[i].clear();
      cur_pos += cnt_feat;
      row_ptr_[i + 1] = cur_pos;
    }
    push_buf_.clear();
    push_buf_.shrink_to_fit();
    data_.shrink_to_fit();
  }

  void LoadFromMemory(const void* memory, const std::vector<data_size_t>& local_used_indices) override {
    const char* mem_ptr = reinterpret_cast<const char*>(memory);
    const data_size_t mem_num_data = *reinterpret_cast<const data_size_t*>(mem_ptr);

    mem_ptr += sizeof(data_size_t);
    const data_size_t* mem_row_ptr_data = reinterpret_cast<const data_size_t*>(mem_ptr);

    mem_ptr += sizeof(data_size_t) * (mem_num_data + 1);
    const VAL_T* mem_data = reinterpret_cast<const VAL_T*>(mem_ptr);

    if (!local_used_indices.empty()) {
      row_ptr_.resize(num_data_ + 1, 0);
      data_.clear();
      for (data_size_t i = 0; i < num_data_; ++i) {
        for (data_size_t j = mem_row_ptr_data[local_used_indices[i]]; j < mem_row_ptr_data[local_used_indices[i] + 1]; ++j) {
          data_.push_back(mem_data[j]);
        }
        row_ptr_[i + 1] = row_ptr_[i] + mem_row_ptr_data[local_used_indices[i] + 1] - mem_row_ptr_data[local_used_indices[i]];
      }
    } else {
      for (data_size_t i = 0; i < num_data_ + 1; ++i) {
        row_ptr_[i] = mem_row_ptr_data[i];
      }
      data_.resize(row_ptr_[num_data_]);
      for (data_size_t i = 0; i < row_ptr_[num_data_]; ++i) {
        data_[i] = mem_data[i];
      }
    }
  }

  void CopySubset(const Bin* full_bin, const data_size_t* used_indices, data_size_t num_used_indices) override {
    auto other_bin = dynamic_cast<const MultiValDenseBin<VAL_T>*>(full_bin);
    row_ptr_.resize(num_data_ + 1, 0);
    data_.clear();
    for (data_size_t i = 0; i < num_used_indices; ++i) {
      for (data_size_t j = other_bin->row_ptr_[used_indices[i]]; j < other_bin->row_ptr_[used_indices[i] + 1]; ++j) {
        data_.push_back(other_bin->data_[j]);
      }
      row_ptr_[i + 1] = row_ptr_[i] + other_bin->row_ptr_[used_indices[i] + 1] - other_bin->row_ptr_[used_indices[i]];
    }
  }

  void SaveBinaryToFile(const VirtualFileWriter* writer) const override {
    writer->Write(&num_data_, sizeof(data_size_t));
    writer->Write(row_ptr_.data(), sizeof(data_size_t) * (num_data_ + 1));
    writer->Write(data_.data(), sizeof(VAL_T) * data_.size());
  }

  inline data_size_t RowPtr(data_size_t idx) const {
    return row_ptr_[idx];
  }

  inline VAL_T GetRawBin(data_size_t idx, VAL_T min_bin, VAL_T max_bin, VAL_T most_freq_bin) const {
    data_size_t low = RowPtr(idx);
    const data_size_t ub = RowPtr(idx + 1);
    data_size_t high = ub;
    while (low < high) {
      data_size_t mid = (low + high) >> 1;
      if (data_[mid] < min_bin) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    if (low < ub && data_[low] >= min_bin && data_[low] <= max_bin) {
      return data_[low];
    } else {
      return most_freq_bin;
    }
  }

  inline VAL_T GetBin(data_size_t idx, VAL_T min_bin, VAL_T max_bin, VAL_T most_freq_bin, int8_t offset) const {
    data_size_t low = RowPtr(idx);
    const data_size_t ub = RowPtr(idx + 1);
    data_size_t high = ub;
    while (low < high) {
      data_size_t mid = (low + high) >> 1;
      if (data_[mid] < min_bin) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    if (low < ub && data_[low] >= min_bin && data_[low] <= max_bin) {
      return data_[low] - min_bin + offset;
    } else {
      return most_freq_bin;
    }
  }

  size_t SizesInByte() const override {
    return sizeof(data_size_t) * (num_data_ + 2) + sizeof(VAL_T) * data_.size();
  }

  MultiValDenseBin<VAL_T>* Clone() override;

private:
  data_size_t num_data_;
  std::vector<VAL_T> data_;
  std::vector<data_size_t> row_ptr_;
  uint8_t shift_;
  std::vector<std::vector<VAL_T>> push_buf_;

  MultiValDenseBin<VAL_T>(const MultiValDenseBin<VAL_T>& other)
    : num_data_(other.num_data_), data_(other.data_) {
  }
};

template<typename VAL_T>
MultiValDenseBin<VAL_T>* MultiValDenseBin<VAL_T>::Clone() {
  return new MultiValDenseBin<VAL_T>(*this);
}

template <typename VAL_T>
uint32_t MultiValDenseBinIterator<VAL_T>::Get(data_size_t idx) {
  return bin_data_->GetBin(idx, min_bin_, max_bin_, most_freq_bin_, offset_);
}


template <typename VAL_T>
BinIterator* MultiValDenseBin<VAL_T>::GetIterator(uint32_t min_bin, uint32_t max_bin, uint32_t most_freq_bin) const {
  return new MultiValDenseBinIterator<VAL_T>(this, min_bin, max_bin, most_freq_bin);
}


}  // namespace LightGBM
#endif   // LIGHTGBM_IO_MULTI_VAL_DENSE_BIN_HPP_
