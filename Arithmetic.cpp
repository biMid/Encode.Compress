#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

double culculateTime(clock_t start, clock_t end) {
  // 返回以ms计算的时间
  return (double)((end - start) * 1000) / CLOCKS_PER_SEC;
}

// --- Shared Configuration & Model ---
const int PRECISION_BITS = 32; // Using 32-bit precision for calculations

const uint64_t TOP_VALUE = (1ULL << PRECISION_BITS) - 1;
const uint64_t FIRST_QUARTER = (TOP_VALUE / 4) + 1;
const uint64_t HALF = (TOP_VALUE / 2) + 1;
const uint64_t THIRD_QUARTER = FIRST_QUARTER * 3;

struct SymbolInfo {
  char symbol;
  uint64_t frequency;
  uint64_t cumulative_low; // Inclusive start of range in total frequency
  uint64_t
      cumulative_high; // Exclusive end of range (cumulative_low + frequency)
};

std::map<char, SymbolInfo> probability_model_global; // Shared model
uint64_t total_frequency_count_global;               // Shared total frequency

char EOF_SYMBOL_CONST; // Will be set in main

// --- Encoder State & Functions ---
uint64_t enc_low;
uint64_t enc_high;
uint64_t enc_pending_underflow_bits;
std::string enc_output_bits_stream;

void enc_output_bit(int bit) { enc_output_bits_stream += (bit ? '1' : '0'); }

void enc_output_bit_plus_pending(int bit) {
  enc_output_bit(bit);
  for (uint64_t i = 0; i < enc_pending_underflow_bits; ++i) {
    enc_output_bit(!bit);
  }
  enc_pending_underflow_bits = 0;
}

void enc_renormalize() {
  while (true) {
    if (enc_high < HALF) {
      enc_output_bit_plus_pending(0);
      enc_low = enc_low * 2;
      enc_high = enc_high * 2 + 1;
    } else if (enc_low >= HALF) {
      enc_output_bit_plus_pending(1);
      enc_low = (enc_low - HALF) * 2;
      enc_high = (enc_high - HALF) * 2 + 1;
    } else if (enc_low >= FIRST_QUARTER && enc_high < THIRD_QUARTER) {
      enc_pending_underflow_bits++;
      enc_low = (enc_low - FIRST_QUARTER) * 2;
      enc_high = (enc_high - FIRST_QUARTER) * 2 + 1;
    } else {
      break;
    }
  }
}

void initialize_encoder_state() {
  enc_low = 0;
  enc_high = TOP_VALUE;
  enc_pending_underflow_bits = 0;
  enc_output_bits_stream.clear();
}

void encode_symbol(char symbol_to_encode) {
  if (probability_model_global.find(symbol_to_encode) ==
      probability_model_global.end()) {
    throw std::runtime_error(
        "Symbol not found in probability model during encoding: " +
        std::string(1, symbol_to_encode));
  }
  const SymbolInfo &sym_info = probability_model_global.at(symbol_to_encode);

  uint64_t current_range = enc_high - enc_low + 1;

  // Update low and high based on the symbol's frequency range
  // cumulative_high is exclusive, so direct multiplication works for upper
  // bound
  enc_high = enc_low +
             (current_range * sym_info.cumulative_high /
              total_frequency_count_global) -
             1;
  enc_low = enc_low + (current_range * sym_info.cumulative_low /
                       total_frequency_count_global);

  enc_renormalize();
}

void flush_encoder() {
  enc_pending_underflow_bits++;
  if (enc_low < FIRST_QUARTER) {
    enc_output_bit_plus_pending(0);
  } else {
    enc_output_bit_plus_pending(1);
  }
}

// --- Decoder State & Functions ---
uint64_t dec_low;
uint64_t dec_high;
uint64_t dec_current_code_value; // Value derived from input bitstream
const std::string *dec_input_bits_stream_ptr;
size_t dec_current_bit_idx;

int read_next_bit_for_decoder() {
  if (dec_input_bits_stream_ptr &&
      dec_current_bit_idx < dec_input_bits_stream_ptr->length()) {
    return (*dec_input_bits_stream_ptr)[dec_current_bit_idx++] - '0';
  }
  return 0; // Assume trailing zeros if stream ends or not set
}

void dec_renormalize() {
  while (true) {
    if (dec_high < HALF) {
      dec_low = dec_low * 2;
      dec_high = dec_high * 2 + 1;
      dec_current_code_value =
          dec_current_code_value * 2 + read_next_bit_for_decoder();
    } else if (dec_low >= HALF) {
      dec_low = (dec_low - HALF) * 2;
      dec_high = (dec_high - HALF) * 2 + 1;
      dec_current_code_value =
          (dec_current_code_value - HALF) * 2 + read_next_bit_for_decoder();
    } else if (dec_low >= FIRST_QUARTER && dec_high < THIRD_QUARTER) {
      dec_low = (dec_low - FIRST_QUARTER) * 2;
      dec_high = (dec_high - FIRST_QUARTER) * 2 + 1;
      dec_current_code_value = (dec_current_code_value - FIRST_QUARTER) * 2 +
                               read_next_bit_for_decoder();
    } else {
      break;
    }
  }
}

void initialize_decoder_state(const std::string &input_bits) {
  dec_low = 0;
  dec_high = TOP_VALUE;
  dec_current_code_value = 0;
  dec_input_bits_stream_ptr = &input_bits;
  dec_current_bit_idx = 0;

  for (int i = 0; i < PRECISION_BITS; ++i) {
    if (dec_current_bit_idx >= input_bits.length() && i > 0) {
      // This case should ideally not be hit if encoder flushed enough bits
      // or if stream is not extremely short.
      // For robustness, we can shift in zeros.
      dec_current_code_value = (dec_current_code_value << 1);
    } else {
      dec_current_code_value =
          (dec_current_code_value << 1) | read_next_bit_for_decoder();
    }
  }
}

void build_shared_probability_model(const std::string &text) {
  probability_model_global.clear();
  std::map<char, uint64_t> freqs;
  for (char c : text) {
    freqs[c]++;
  }
  freqs[EOF_SYMBOL_CONST]++; // Add EOF frequency

  total_frequency_count_global = 0;
  for (auto const &[symbol, count] : freqs) {
    total_frequency_count_global += count;
  }
  if (total_frequency_count_global == 0) {
    throw std::runtime_error("Cannot build model for empty text + EOF.");
  }

  uint64_t current_cumulative_low = 0;
  // Iterate over sorted keys (chars) for consistent model
  for (auto const &pair : freqs) {
    char symbol = pair.first;
    uint64_t freq = pair.second;
    if (freq == 0)
      continue;

    SymbolInfo info;
    info.symbol = symbol;
    info.frequency = freq;
    info.cumulative_low = current_cumulative_low;
    info.cumulative_high =
        current_cumulative_low + freq; // cumulative_high is exclusive
    probability_model_global[symbol] = info;

    current_cumulative_low += freq;
  }
}

std::string arithmetic_encode(const std::string &input_text) {
  initialize_encoder_state();
  // Probability model assumed to be pre-built by build_shared_probability_model

  if (total_frequency_count_global == 0) {
    throw std::runtime_error(
        "Total frequency count is zero. Model not built or empty.");
  }
  if (probability_model_global.empty()) {
    throw std::runtime_error("Probability model is empty.");
  }

  for (char c : input_text) {
    encode_symbol(c);
  }
  encode_symbol(EOF_SYMBOL_CONST);

  flush_encoder();
  return enc_output_bits_stream;
}

std::string arithmetic_decode(const std::string &encoded_bits) {
  initialize_decoder_state(encoded_bits);
  // Probability model assumed to be pre-built

  if (total_frequency_count_global == 0) {
    throw std::runtime_error(
        "Total frequency count is zero for decoder. Model not built or empty.");
  }
  if (probability_model_global.empty()) {
    throw std::runtime_error("Probability model is empty for decoder.");
  }

  std::string decoded_text;
  while (true) {
    uint64_t current_range = dec_high - dec_low + 1;
    if (current_range == 0) { // Should not happen with proper renormalization
      throw std::runtime_error("Decoder range became zero.");
    }

    // Find where current_code_value falls within the
    // total_frequency_count_global scale
    // ((value_in_interval - interval_low) * total_counts_in_model) /
    // interval_range Note: +1 on current_range for the division if dec_low and
    // dec_high are inclusive. Here, dec_low and dec_high define an inclusive
    // range [dec_low, dec_high]. The scaled value represents a point in the
    // cumulative frequency distribution.
    uint64_t search_target_freq =
        ((dec_current_code_value - dec_low) * total_frequency_count_global) /
        current_range;

    char current_decoded_symbol =
        EOF_SYMBOL_CONST; // Default to EOF, will be updated
    bool found_symbol = false;

    for (auto const &pair :
         probability_model_global) { // Iterate in sorted order
      const SymbolInfo &sym_info = pair.second;
      // Check if search_target_freq falls into this symbol's cumulative range
      // [sym_info.cumulative_low, sym_info.cumulative_high)
      if (search_target_freq >= sym_info.cumulative_low &&
          search_target_freq < sym_info.cumulative_high) {
        current_decoded_symbol = sym_info.symbol;

        // Update decoder's low and high to this symbol's sub-interval
        // Mirroring encoder:
        // enc_high = enc_low + (current_range * sym_info.cumulative_high /
        // total_frequency_count_global) - 1; enc_low = enc_low + (current_range
        // * sym_info.cumulative_low / total_frequency_count_global);
        dec_high = dec_low +
                   (current_range * sym_info.cumulative_high /
                    total_frequency_count_global) -
                   1;
        dec_low = dec_low + (current_range * sym_info.cumulative_low /
                             total_frequency_count_global);

        found_symbol = true;
        break;
      }
    }

    if (!found_symbol) {
      // This might happen if search_target_freq equals
      // total_frequency_count_global due to precision at the very end. This
      // typically means it's the last symbol in the model if sorted by
      // cumulative frequency. However, the EOF symbol logic should handle
      // graceful termination. If truly not found, it's an error.
      throw std::runtime_error(
          "Decoder failed to find symbol for current code value. "
          "Target_freq: " +
          std::to_string(search_target_freq) +
          ", Total_freq: " + std::to_string(total_frequency_count_global));
    }

    if (current_decoded_symbol == EOF_SYMBOL_CONST) {
      break; // EOF symbol decoded, end of data
    }
    decoded_text += current_decoded_symbol;

    dec_renormalize();
  }
  return decoded_text;
}

int main() {
  // 读取文件内容
  std::ifstream file("input.txt");
  if (!file.is_open()) {
    std::cerr << "无法打开文件" << std::endl;
    return 1;
  }
  std::string original_text((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  file.close();

  EOF_SYMBOL_CONST = '\3'; // ETX (End of Text) as EOF symbol

  build_shared_probability_model(original_text);

  // 编码
  std::string compressed_bits = arithmetic_encode(original_text);

  // 解码
  std::string decoded_text = arithmetic_decode(compressed_bits);

  // 检查解码是否正确
  if (decoded_text == original_text) {
    std::cout << "Decoded successfully!" << std::endl;
  } else {
    std::cout << "Decoding failed!" << std::endl;
    return 0;
  }

  double entropy = 4.42954; // 信源熵
  // 计算平均编码长度
  double avg_length =
      compressed_bits.length() / (double)(original_text.length() + 1);
  std::cout << "Average length: " << avg_length << std::endl;
  std::cout << "Compression Ratio: " << (entropy / avg_length) * 100 << "%"
            << std::endl; // 输出编码效率

  // 统计编码时间消耗
  clock_t start = clock();
  for (int i = 0; i < 100; i++) {
    arithmetic_encode(original_text);
  }
  clock_t end = clock();
  std::cout << "Encoding Time: " << culculateTime(start, end) / 100.0 << " ms"
            << std::endl;

  // 统计解码时间消耗
  start = clock();
  for (int i = 0; i < 100; i++) {
    arithmetic_decode(compressed_bits);
  }
  end = clock();
  std::cout << "Decoding Time: " << culculateTime(start, end) / 100.0 << " ms"
            << std::endl;

  return 0;
}
