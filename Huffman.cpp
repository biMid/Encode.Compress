#include <fstream>
#include <iostream>
#include <math.h>
#include <queue>
#include <unordered_map>
#include <vector>

using namespace std;

// 定义霍夫曼树节点结构体
struct Node {
  char ch;            // 字符
  int freq;           // 频率
  Node *left, *right; // 左右子节点指针
};

// 比较函数，用于优先队列，按频率降序排列
struct compare {
  bool operator()(Node *l, Node *r) { return l->freq > r->freq; }
};

double culculateTime(clock_t start, clock_t end) {
  // 返回以ms计算的时间
  return (double)((end - start) * 1000) / CLOCKS_PER_SEC;
}

// 递归打印霍夫曼编码
void printCodes(Node *root, string str,
                unordered_map<char, string> &huffmanCode) {
  if (!root)
    return; // 如果节点为空，返回
  if (!root->left && !root->right) {
    huffmanCode[root->ch] = str; // 如果是叶子节点，存储字符对应的霍夫曼编码
  }
  printCodes(root->left, str + "0", huffmanCode);  // 递归遍历左子树，编码加'0'
  printCodes(root->right, str + "1", huffmanCode); // 递归遍历右子树，编码加'1'
}


string encode(string text, unordered_map<char, string> &huffmanCode) {
  string encodedText = ""; // 初始化编码后的字符串
  for (char ch : text) {
    encodedText += huffmanCode[ch]; // 将每个字符替换为其霍夫曼编码
  }
  return encodedText;
}

string decode(Node *root, string encodedText) {
  string decodedText = ""; // 初始化解码后的字符串
  Node *curr = root;       // 当前节点指向根节点
  for (int i = 0; i < encodedText.size(); ++i) {
    if (encodedText[i] == '0')
      curr = curr->left; // 如果当前位为'0'，移动到左子节点
    else
      curr = curr->right; // 如果当前位为'1'，移动到右子节点

    if (curr->left == nullptr && curr->right == nullptr) {
      decodedText += curr->ch; // 如果到达叶子节点，添加字符到解码字符串
      curr = root;             // 重置当前节点为根节点
    }
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

  unordered_map<char, int> frequency; // 存储字符频率的哈希表

  // 计算每个字符的频率
  for (char ch : text) {
    frequency[ch]++;
  }

  // 创建优先队列，按频率升序排列
  priority_queue<Node *, vector<Node *>, compare> heap;

  // 将所有字符作为单独节点插入优先队列
  for (auto pair : frequency) {
    heap.push(new Node({pair.first, pair.second, nullptr, nullptr}));
  }

  clock_t start = clock();
  // 构建霍夫曼树
  while (heap.size() != 1) {
    Node *left = heap.top();
    heap.pop();
    Node *right = heap.top();
    heap.pop();

    int sum = left->freq + right->freq;
    heap.push(new Node({'\0', sum, left, right})); // 合并两个频率最小的节点
  }

  // 获取每个字符的霍夫曼编码
  Node *root = heap.top(); // 根节点指向霍夫曼树的根
  unordered_map<char, string> huffmanCode;
  printCodes(root, "", huffmanCode);
  clock_t end = clock();

  // 编码
  string encodedText = encode(text, huffmanCode);

  // 解码
  string decodedText = decode(root, encodedText);

  // 检查解码是否正确
  if (text == decodedText) {
    cout << "Decoded successfully!" << endl;
  } else {
    cout << "Decoding failed!" << endl;
  }

  // 计算信源的熵
  double entropy = 0.0;
  int totalChars = text.size();
  for (auto pair : frequency) {
    double prob = (double)pair.second / totalChars; // 计算概率
    entropy -= prob * log2(prob);                   // 计算熵
  }

  // 计算平均长度
  double avgLength = encodedText.size() / (double)totalChars;

  cout << "Entropy: " << entropy << endl;          // 输出信源熵
  cout << "Average Length: " << avgLength << endl; // 输出平均长度
  cout << "Compression Ratio: " << (entropy / avgLength) * 100 << "%"
       << endl; // 输出编码效率

  // 统计构建霍夫曼树的时间开销
  cout << "Huffman Tree Construction Time: "
       << culculateTime(start, end) / 100.0 << " ms" << endl;

  // 统计编码时间开销
  start = clock();
  for (int i = 0; i < 100; i++) {
    string encodedText = encode(text, huffmanCode);
  }
  end = clock();
  cout << "Encoding Time: " << culculateTime(start, end) / 100.0 << " ms"
       << endl;

  // 统计解码时间开销
  start = clock();
  for (int i = 0; i < 100; i++) {
    string decodedText = decode(root, encodedText);
  }
  end = clock();
  cout << "Decoding Time: " << culculateTime(start, end) / 100.0 << " ms"
       << endl;

  // 输出霍夫曼编码结果到文件
  ofstream encodedFile("encodedText.txt");
  if (!encodedFile.is_open()) {
    cerr << "Failed to open encoded file." << endl;
    return 1;
  }
  encodedFile << encodedText << endl;
  encodedFile.close();

  // 输出霍夫曼编码表到文件
  ofstream huffmanCodeFile("huffmanCode.txt");
  if (!huffmanCodeFile.is_open()) {
    cerr << "Failed to open huffman code file." << endl;
    return 1;
  }
  for (const auto &pair : huffmanCode) {
    huffmanCodeFile << pair.first << " -> " << pair.second << endl;
  }
  huffmanCodeFile.close();

  return 0;
}