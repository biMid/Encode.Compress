#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// --- 共享配置和数据 ---
const int PRECISION_BITS = 32; // 使用32位精度进行计算

const uint64_t TOP_VALUE = (1ULL << PRECISION_BITS) - 1;
const uint64_t FIRST_QUARTER = (TOP_VALUE / 4) + 1;
const uint64_t HALF = (TOP_VALUE / 2) + 1;
const uint64_t THIRD_QUARTER = FIRST_QUARTER * 3;

struct SymbolInfo {
  char symbol;
  uint64_t frequency;
  uint64_t cumulative_low;
  uint64_t cumulative_high;
};

std::map<char, SymbolInfo> probability_model_global; // 共享模型
uint64_t total_frequency_count_global;               // 共享总频率

char EOF_SYMBOL_CONST; // 结束符号

// --- 通用函数 ---
double culculateTime(clock_t start, clock_t end) {
  // 返回以ms计算的时间
  return (double)((end - start) * 1000) / CLOCKS_PER_SEC;
}

void build_shared_probability_model(const std::string &text) {
  probability_model_global.clear();
  std::map<char, uint64_t> freqs;
  for (char c : text) {
    freqs[c]++;
  }
  freqs[EOF_SYMBOL_CONST]++; // 添加EOF频率

  total_frequency_count_global = 0;
  for (auto const &[symbol, count] : freqs) {
    total_frequency_count_global += count;
  }

  uint64_t current_cumulative_low = 0;
  for (auto const &pair : freqs) {
    char symbol = pair.first;
    uint64_t freq = pair.second;
    if (freq == 0)
      continue;

    SymbolInfo info;
    info.symbol = symbol;
    info.frequency = freq;
    info.cumulative_low = current_cumulative_low;
    info.cumulative_high = current_cumulative_low + freq;

    probability_model_global[symbol] = info;

    current_cumulative_low += freq;
  }
}

// --- 编码器状态和函数 ---
uint64_t enc_low;
uint64_t enc_high;
uint64_t enc_pending_underflow_bits;
std::string enc_output_bits_stream;

void enc_output_bit(int bit) { enc_output_bits_stream += (bit ? '1' : '0'); }

void enc_output_bit_plus_pending(int bit) {
  enc_output_bit(bit);
  for (uint64_t i = 0; i < enc_pending_underflow_bits; i++) {
    enc_output_bit(!bit);
  }
  enc_pending_underflow_bits = 0;
}

void enc_renormalize() {
  while (true) {
    if (enc_high < HALF) { // 如果编码器的上限小于一半，可以确定下一个比特为0
      enc_output_bit_plus_pending(0);
      enc_low = enc_low * 2;
      enc_high = enc_high * 2 + 1;
    } else if (enc_low >=
               HALF) { // 如果编码器的下限大于等于一半，可以确定下一个比特为1
      enc_output_bit_plus_pending(1);
      enc_low = (enc_low - HALF) * 2;
      enc_high = (enc_high - HALF) * 2 + 1;
    } else if (
        enc_low >= FIRST_QUARTER &&
        enc_high <
            THIRD_QUARTER) { // 如果编码器的下限大于等于四分之一且上限小于四分之三，无法确定下一个比特，需要等待更多比特
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
  const SymbolInfo &sym_info = probability_model_global.at(symbol_to_encode);

  uint64_t current_range = enc_high - enc_low + 1;

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

std::string arithmetic_encode(const std::string &input_text) {
  initialize_encoder_state();

  for (char c : input_text) {
    encode_symbol(c);
  }
  encode_symbol(EOF_SYMBOL_CONST);

  flush_encoder();
  return enc_output_bits_stream;
}

// --- 解码器状态和函数 ---
uint64_t dec_low;
uint64_t dec_high;
uint64_t dec_current_code_value; // 从输入比特流派生出的值
const std::string *dec_input_bits_stream_ptr;
size_t dec_current_bit_idx;

int read_next_bit_for_decoder() {
  if (dec_input_bits_stream_ptr &&
      dec_current_bit_idx < dec_input_bits_stream_ptr->length()) {
    return (*dec_input_bits_stream_ptr)[dec_current_bit_idx++] - '0';
  }
  return 0;
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
    dec_current_code_value =
        (dec_current_code_value << 1) | read_next_bit_for_decoder();
  }
}

std::string arithmetic_decode(const std::string &encoded_bits) {
  initialize_decoder_state(encoded_bits);

  std::string decoded_text;
  while (true) {
    uint64_t current_range = dec_high - dec_low + 1;

    // 找到当前代码值在总频率范围内的位置
    // ((value_in_interval - interval_low) * total_counts_in_model) /
    // interval_range
    // 注意：如果dec_low和dec_high是包含的，则current_range需要加1
    // 这里，dec_low和dec_high定义了一个包含的范围[dec_low, dec_high]，
    // 缩放值表示在累积频率分布中的一个点。
    uint64_t search_target_freq =
        ((dec_current_code_value - dec_low) * total_frequency_count_global) /
        current_range;

    char current_decoded_symbol = EOF_SYMBOL_CONST;
    bool found_symbol = false;

    for (auto const &pair : probability_model_global) {
      const SymbolInfo &sym_info = pair.second;
      // 检查search_target_freq是否落在该符号的累积范围内
      if (search_target_freq >= sym_info.cumulative_low &&
          search_target_freq < sym_info.cumulative_high) {
        current_decoded_symbol = sym_info.symbol;

        // 更新解码器的下限和上限到该符号的子区间
        // 镜像编码器
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

    if (current_decoded_symbol == EOF_SYMBOL_CONST)
      break;

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
