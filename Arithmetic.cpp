#include <bitset>
#include <cmath>
#include <fstream>
#include <gmp.h>
#include <gmpxx.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace std;

class ArithmeticCoding {
public:
  static const int SCALE_FACTOR = 1000000; // 缩放因子，用于将概率转换为整数
  static const int RENORMALIZATION_THRESHOLD = 0x40000000; // 重归一化阈值

  struct SymbolRange {
    mpz_t low;   // 区间下界
    mpz_t high;  // 区间上界
    mpz_t count; // 符号计数
  };

  map<char, SymbolRange> symbolMap;
  mpz_t totalCount; // 总计数
  mpz_t scale;      // 缩放因子

  ArithmeticCoding() {
    mpz_init(totalCount);
    mpz_init_set_ui(scale, SCALE_FACTOR);
  }

  ~ArithmeticCoding() {
    for (auto &pair : symbolMap) {
      mpz_clear(pair.second.low);
      mpz_clear(pair.second.high);
      mpz_clear(pair.second.count);
    }
    mpz_clear(totalCount);
    mpz_clear(scale);
  }

  void buildModel(const string &text) {
    // 清理旧的模型
    for (auto &pair : symbolMap) {
      mpz_clear(pair.second.low);
      mpz_clear(pair.second.high);
      mpz_clear(pair.second.count);
    }
    symbolMap.clear();
    mpz_set_ui(totalCount, 0);

    // 统计频率
    map<char, int> freqMap;
    for (char c : text) {
      freqMap[c]++;
    }

    // 初始化符号范围
    mpz_t cumulative;
    mpz_init_set_ui(cumulative, 0);

    for (const auto &pair : freqMap) {
      SymbolRange range;
      mpz_init_set(range.low, cumulative);
      mpz_init_set_ui(range.count, pair.second);
      mpz_add(cumulative, cumulative, range.count);
      mpz_init_set(range.high, cumulative);
      symbolMap[pair.first] = range;
    }

    mpz_set(totalCount, cumulative);
    mpz_clear(cumulative);
  }

  pair<string, int> encode(const string &text) {
    mpz_t low, high, range, temp;
    mpz_init_set_ui(low, 0);
    mpz_init_set_ui(high, SCALE_FACTOR);
    mpz_init(range);
    mpz_init(temp);

    string encodedBits;
    int numBits = 0;

    for (char c : text) {
      // 显示编码进度
      static int processedChars = 0;
      processedChars++;
      if (processedChars % 100 == 0 || processedChars == text.length()) {
        cout << "\r编码进度: " << (processedChars * 100.0 / text.length())
             << "%" << flush;
      }

      // 计算当前区间范围
      mpz_sub(range, high, low);

      // 计算新的区间
      mpz_mul(temp, range, symbolMap[c].low);
      mpz_div(temp, temp, totalCount);
      mpz_add(low, low, temp);

      mpz_mul(temp, range, symbolMap[c].high);
      mpz_div(temp, temp, totalCount);
      mpz_add(high, low, temp);

      // 重归一化
      while (true) {
        if (mpz_cmp_ui(high, RENORMALIZATION_THRESHOLD) <= 0) {
          // 区间在左半部分
          encodedBits += '0';
          mpz_mul_2exp(low, low, 1);
          mpz_mul_2exp(high, high, 1);
          break;
        } else if (mpz_cmp_ui(low, RENORMALIZATION_THRESHOLD) >= 0) {
          // 区间在右半部分
          encodedBits += '1';
          mpz_sub_ui(low, low, RENORMALIZATION_THRESHOLD);
          mpz_sub_ui(high, high, RENORMALIZATION_THRESHOLD);
          mpz_mul_2exp(low, low, 1);
          mpz_mul_2exp(high, high, 1);
          break;
        } else if (mpz_cmp_ui(low, RENORMALIZATION_THRESHOLD / 2) >= 0 &&
                   mpz_cmp_ui(high, RENORMALIZATION_THRESHOLD * 3 / 2) <= 0) {
          // 区间在中间部分
          mpz_sub_ui(low, low, RENORMALIZATION_THRESHOLD / 2);
          mpz_sub_ui(high, high, RENORMALIZATION_THRESHOLD / 2);
          mpz_mul_2exp(low, low, 1);
          mpz_mul_2exp(high, high, 1);
        } else {
          break;
        }
        numBits++;
      }
    }

    // 添加最后一个比特
    if (mpz_cmp_ui(low, RENORMALIZATION_THRESHOLD / 2) >= 0) {
      encodedBits += '1';
    } else {
      encodedBits += '0';
    }
    numBits++;

    // 清理
    mpz_clear(low);
    mpz_clear(high);
    mpz_clear(range);
    mpz_clear(temp);

    return {encodedBits, numBits};
  }

  string decode(const string &encodedBits, size_t length) {
    mpz_t value, range, temp;
    mpz_init_set_ui(value, 0);
    mpz_init_set_ui(range, SCALE_FACTOR);
    mpz_init(temp);

    // 将编码的二进制字符串转换为数值
    for (char bit : encodedBits) {
      mpz_mul_2exp(value, value, 1);
      if (bit == '1') {
        mpz_add_ui(value, value, 1);
      }
    }

    string decodedText;
    mpz_t low, high;
    mpz_init_set_ui(low, 0);
    mpz_init_set_ui(high, SCALE_FACTOR);

    for (size_t i = 0; i < length; i++) {
      // 计算当前区间范围
      mpz_sub(range, high, low);

      // 查找对应的符号
      for (const auto &pair : symbolMap) {
        mpz_mul(temp, range, pair.second.low);
        mpz_div(temp, temp, totalCount);
        mpz_add(temp, low, temp);

        mpz_mul(temp, range, pair.second.high);
        mpz_div(temp, temp, totalCount);
        mpz_add(temp, low, temp);

        if (mpz_cmp(value, temp) >= 0 && mpz_cmp(value, temp) < 0) {
          decodedText += pair.first;
          break;
        }
      }
    }

    // 清理
    mpz_clear(value);
    mpz_clear(range);
    mpz_clear(temp);
    mpz_clear(low);
    mpz_clear(high);

    return decodedText;
  }

  double entropy() const {
    double entropyValue = 0.0;
    for (const auto &pair : symbolMap) {
      double prob = mpz_get_d(pair.second.count) / mpz_get_d(totalCount);
      if (prob > 0) {
        entropyValue -= prob * log2(prob);
      }
    }
    return entropyValue;
  }
};

int main() {
  // 读取文件内容
  ifstream file("input.txt");
  if (!file.is_open()) {
    cerr << "无法打开文件" << endl;
    return 1;
  }
  string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
  file.close();

  // 创建算术编码器
  ArithmeticCoding ac;
  ac.buildModel(text);

  // 输出概率分布
  cout << "符号概率分布：" << endl;
  for (const auto &pair : ac.symbolMap) {
    cout << pair.first << ": "
         << mpz_get_d(pair.second.count) / mpz_get_d(ac.totalCount) << endl;
  }

  // 编码
  auto [encodedBits, numBits] = ac.encode(text);
  cout << "编码后的比特数：" << numBits << endl;
  cout << "编码结果：" << encodedBits << endl;

  // 解码
  string decodedText = ac.decode(encodedBits, text.length());
  cout << "解码结果：" << decodedText << endl;

  // 计算编码效率
  double entropyValue = ac.entropy();
  double avgLength = static_cast<double>(numBits) / text.length();
  double compressionRatio = (entropyValue / avgLength) * 100;

  cout << "熵值：" << entropyValue << endl;
  cout << "平均码长：" << avgLength << endl;
  cout << "压缩比：" << compressionRatio << "%" << endl;

  return 0;
}