#include <bitset>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

unordered_map<char, string> symbolTable;
unordered_map<string, char> reverseSymbolTable;
int symbolBits = 0;
int segBits = 0;

double culculateTime(clock_t start, clock_t end) {
  // 返回以ms计算的时间
  return (double)((end - start) * 1000) / CLOCKS_PER_SEC;
}

void buildSymbolTable(const string &input) {
  int dictSize = 0;
  unordered_map<char, int> tempSymbolTable;
  // 统计所有的字符，得到符号编码表
  for (char c : input) {
    if (tempSymbolTable.find(c) ==
        tempSymbolTable.end()) { // 如果字符不在符号表中
      tempSymbolTable[c] = dictSize++;
    }
  }

  // 计算编码所需的位数
  dictSize = tempSymbolTable.size();
  while ((1 << symbolBits) < dictSize) {
    symbolBits++;
  }

  // 将int值转换为二进制字符串，存入符号表
  for (const auto &pair : tempSymbolTable) {
    string binaryString =
        bitset<8>(pair.second).to_string().substr(8 - symbolBits);
    symbolTable[pair.first] = binaryString;
  }
}

void buildReverseSymbolTable() {
  for (const auto &pair : symbolTable) {
    reverseSymbolTable[pair.second] = pair.first;
  }
}

string lz78Encode(const string &input) {
  unordered_map<string, int> dictionary;
  vector<pair<int, string>> encodedData;
  string encodedBits;

  int dictSize = 1;
  string currentString = "";
  // 分段，得到字典，并进行初步编码
  for (char c : input) {
    currentString += c;
    if (dictionary.find(currentString) == dictionary.end()) {
      // 如果当前字符串不在字典中
      int index =
          currentString.length() > 1
              ? dictionary[currentString.substr(0, currentString.length() - 1)]
              : 0;
      char lastChar = currentString.back();
      string lastCharBits = symbolTable[lastChar];
      encodedData.push_back(make_pair(index, lastCharBits));
      dictionary[currentString] = dictSize++;
      currentString = "";
    }
  }
  // 处理最后一个字符
  if (!currentString.empty()) {
    int index =
        currentString.length() > 1
            ? dictionary[currentString.substr(0, currentString.length() - 1)]
            : 0;
    char lastChar = currentString.back();
    string lastCharBits = symbolTable[lastChar];
    encodedData.push_back(make_pair(index, lastCharBits));
  }

  // 计算段号所需的位数
  dictSize = dictionary.size();
  while ((1 << segBits) < dictSize) {
    segBits++;
  }

  // 在初步编码的基础上完成编码
  for (const auto &pair : encodedData) {
    // 将索引转换为二进制字符串
    int index = pair.first;
    string indexBits = bitset<16>(index).to_string().substr(16 - segBits);

    // 拼接编码结果
    encodedBits += indexBits + pair.second;
  }

  return encodedBits;
}

string lz78Decode(const string &encodedBits) {
  string decodedText;
  unordered_map<int, string> dictionary;
  int dictSize = 1;

  // 解码
  size_t pos = 0;
  while (pos < encodedBits.length()) {
    // 提取出段号
    if (pos + segBits > encodedBits.length())
      break;
    string indexBitSequence = encodedBits.substr(pos, segBits);
    pos += segBits;

    // 提取出符号
    if (pos + symbolBits > encodedBits.length())
      break;
    string symbolBitSequence = encodedBits.substr(pos, symbolBits);
    pos += symbolBits;

    // 将索引转换为整数
    int index = bitset<64>(indexBitSequence).to_ulong();

    // 解码
    char nextChar = reverseSymbolTable[symbolBitSequence];
    string decodedString =
        (index > 0) ? dictionary[index] + nextChar : string(1, nextChar);

    decodedText += decodedString;
    dictionary[dictSize++] = decodedString;
  }

  return decodedText;
}

int main() {
  // 读取整个文件内容
  ifstream file("input.txt");
  if (!file.is_open()) {
    cerr << "Failed to open file." << endl;
    return 1;
  }
  string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
  file.close();

  buildSymbolTable(text);    // 构建符号表
  buildReverseSymbolTable(); // 构建化反向符号表

  // 编码
  string encodedText = lz78Encode(text);

  // 解码
  string decodedText = lz78Decode(encodedText);

  // 比较编码和解码结果
  if (text == decodedText) {
    cout << "Encoding and Decoding Successful!" << endl;
  } else {
    cout << "Encoding and Decoding Failed!" << endl;
  }

  // 计算编码效率
  double entropy = 4.42954;
  double avgLength = encodedText.size() / (double)text.size();
  cout << "Entropy: " << entropy << endl;          // 输出信源熵
  cout << "Average Length: " << avgLength << endl; // 输出平均长度
  cout << "Compression Ratio: " << (entropy / avgLength) * 100 << "%"
       << endl; // 输出编码效率

  // 统计编码时间消耗
  clock_t start = clock();
  for (int i = 0; i < 100; i++) {
    lz78Encode(text);
  }
  clock_t end = clock();
  cout << "Encoding Time: " << culculateTime(start, end) / 100.0 << " ms"
       << endl;

  // 统计解码时间消耗
  start = clock();
  for (int i = 0; i < 100; i++) {
    lz78Decode(encodedText);
  }
  end = clock();
  cout << "Decoding Time: " << culculateTime(start, end) / 100.0 << " ms"
       << endl;

  return 0;
}
