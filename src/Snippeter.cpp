#include <sys/stat.h>
#include "Snippeter.h"

namespace gmsnippet {

  using namespace std;

  Snippeter::
  Snippeter(const std::string& filepath)
  {
    loadSearchDocument(filepath);
    parseSearchDocument();
  }

  Snippeter::
  ~Snippeter()
  {
    if (searchDoc_ != nullptr) {
      delete[] searchDoc_;
      searchDoc_ = nullptr;
    }
  }

  void
  Snippeter::
  loadSearchDocument(const std::string& filepath)
  {
    // open file and map in on the process address space
    const int zeroOffset = 0;
    FILE* inputFile = fopen(filepath.c_str(), "r");

    if (inputFile == nullptr) {
      perror("Error");
      throw std::runtime_error("Failed to open file");
    }

    int fileDescriptor = fileno(inputFile);
    long seekResult = lseek(fileDescriptor, zeroOffset, SEEK_END);
    if (seekResult == -1) {
      perror("Error");
      throw std::runtime_error("Failed to get file length");
    }

    searchDocSize_ = (size_t) seekResult;

    char* file = (char*) mmap(nullptr, searchDocSize_, PROT_READ, MAP_PRIVATE, fileDescriptor, zeroOffset);
    fclose(inputFile);

    if (file == MAP_FAILED) {
      perror("Error");
      throw std::runtime_error("Failed to load file into memory");
    }

    // convert mapped region to wide string (input text might
    // have multi-byte encoding (utf-8, utf-16, etc.))
    try {
      searchDoc_ = new wchar_t[searchDocSize_]();
    } catch (std::bad_alloc& e) {
      munmap(file, searchDocSize_);
      throw std::runtime_error("Failed to allocate memory for wide character set file");
    }

    if (mbstowcs(searchDoc_, file, searchDocSize_) == (size_t)-1) {
      munmap(file, searchDocSize_);
      delete[] searchDoc_;
      perror("Error");
      throw std::runtime_error("Failed to switch to wide character set");
    }

    if (munmap(file, searchDocSize_) == -1) {
      delete[] searchDoc_;
      perror("Error");
      throw std::runtime_error("Failed to free mapped file");
    }

  }

  void
  Snippeter::
  parseSearchDocument()
  {
    wchar_t current;
    wchar_t previous = L'.';

    size_t sentenceNumber = 0;
    size_t wordStart = 0;

    offsetTable_.push_back(0);
    for (size_t pos = 0; pos < searchDocSize_ ; ++pos) {

      current = searchDoc_[pos];
      if ((current == L'\n' && previous == L'\n') ||
          current == L'.' || current == L'?'|| current ==  L'!') {
        setTfIdf(wordStart, pos, sentenceNumber);
        offsetTable_.push_back(++pos);
        sentenceNumber++;
        while(pos < searchDocSize_ && !iswalnum((wint_t)searchDoc_[pos])){ pos++; }
        offsetTable_.push_back(pos);
        wordStart = pos;
        sentenceNumber++;
      }
      else if (current == L' ' || current == L',' ||
               current == L':' || current == L';') {
        setTfIdf(wordStart, pos, sentenceNumber);
        wordStart = pos + 1;
      }
      previous = current;

    }

    setTfIdf(wordStart, searchDocSize_, sentenceNumber);
  }

  void
  Snippeter::
  setTfIdf(size_t wordStart, size_t wordEnd, size_t sentenceNumber)
  {
    if (wordStart >= wordEnd) {
      return;
    };

    std::wstring word(searchDoc_ + wordStart, wordEnd - wordStart);

    if (!TextUtils::isalnum(word)) {
      return;
    }

    // unify all terms presentation
    TextUtils::lowercase(word);
    word = TextUtils::trim(word);


    // add up global term frequency
    try {
      occurrencesTable_.at(word) += 1;
    } catch (std::out_of_range& e) {
      occurrencesTable_[word] = 1;
    }

    // register term frequency for currently processed sentence
    try {
      auto& sentences = tfTable_.at(word);

//    the term has either been met in current sentence or not
      if (sentences.back().sentenceNumber == sentenceNumber) {
        sentences.back().tf++;
      } else {
        TFTableEntry entry{sentenceNumber, 1};
        sentences.push_back(entry);
      }
    } catch (std::out_of_range& e) {
      TFTableEntry entry{sentenceNumber, 1};
      tfTable_[word].push_back(entry);
    }
  }

  std::wstring
  Snippeter::
  getSnippet(std::wstring query) const
  {
    if (query.length() == 0) {
      return L"Задан пустой запрос.";
    }

    std::vector<std::wstring> tokens = tokenizeQuery(query);
    if (tokens.empty()) {
      return L"Запрос не содержит слов, по которым можно составить сниппет.";
    }

    sortAndStripTokensSet(tokens, MAX_TOKENS_TO_USE);

    const auto& sentences = getFeasibleSentenceIndexes(tokens);
    return getSnippetFromSentences(sentences, tokens);
  }

  std::vector<std::wstring>
  Snippeter::
  tokenizeQuery(std::wstring &query) const
  {
    TextUtils::lowercase(query);
    std::wstringstream ss(query);
    if (ss.fail()) {
      perror("Error");
      throw std::runtime_error("Couldn't parse query\n");
    }

    // Split query into words, filter unknown ones
    std::vector<std::wstring> queryWords;
    while(!ss.eof()) {
      std::wstring word;
      ss >> word;
      word = TextUtils::trim(word);
      if (word.length() > 0 && TextUtils::isalnum(word) && occurrencesTable_.count(word) > 0) {
        queryWords.push_back(word);
      }
    }

    return queryWords;
  }

  void
  Snippeter::
  sortAndStripTokensSet(vector<wstring>& tokens, unsigned long long maxTokensCount) const
  {
    auto idfWordCmp = [this](const std::wstring& s1, const std::wstring& s2) -> bool {
        return this->occurrencesTable_.at(s1) < this->occurrencesTable_.at(s2);
    };
    // sort tokens vector by occurrence count int the document DESC
    sort(tokens.begin(), tokens.end(), idfWordCmp);
    // make vector contain no more than maxTokensCount values
    tokens.resize(min((unsigned long long)tokens.size(), maxTokensCount));
  }

  unordered_set<Snippeter::sentence_number_t>
  Snippeter::
  getFeasibleSentenceIndexes(const vector<wstring>& tokens) const
  {
    std::unordered_set<Snippeter::sentence_number_t> feasibleSentenceIndexes;

    for (const auto& token : tokens) {
      uint64_t sentencesUsed = 0;
      for (const auto& entry : tfTable_.at(token)) {
        if (sentencesUsed >= 2*MAX_SENTENCES_TO_USE) break;
        sentence_number_t number = entry.sentenceNumber;
        feasibleSentenceIndexes.insert(number);
        sentencesUsed++;
      }
    }

    return feasibleSentenceIndexes;
  }

  std::wstring
  Snippeter::
  getSnippetFromSentences(
          const unordered_set<Snippeter::sentence_number_t>& sentences,
          const vector<wstring>& tokens) const
  {
    vector<SentenceWeighingResult> weighedSentences;

    for (const auto& sentenceIndex : sentences) {
      SentenceWeighingResult result = countSentenceWeight(sentenceIndex, tokens);
      weighedSentences.push_back(result);
    }

    weighedSentences.shrink_to_fit();
    sortSentencesByWeight(weighedSentences);

    // trim to contain no more that MAX_SENTENCES_TO_USE
    const auto maxTokensToUse = MAX_SENTENCES_TO_USE;
    weighedSentences.resize(min((unsigned long long)weighedSentences.size(), maxTokensToUse));

    // decorate sentences if needed
    // ... decoration code ...

    sortSentencesByIndex(weighedSentences);

    return getStringSnippet(weighedSentences);
  }

  Snippeter::SentenceWeighingResult
  Snippeter::countSentenceWeight(
          const sentence_number_t& sentenceNumber,
          const std::vector<std::wstring>& tokens) const
  {
    double weight = 0;
    sentence_number_t sentenceLength = getSentenceLength(sentenceNumber);
    double penaltyScore = 1 + abs(log(BENCHMARK_SENTENCE_LENGTH) - log(sentenceLength));

    wstring tokenToPut = L"";
    uint64_t entryIndexForFirstToken = 0;
    bool isEntryIndexSet = false;

    for (const auto& token : tokens) {
      double idf = searchDocSize_ / occurrencesTable_.at(token);
      double tf = 0;

      unsigned int index = 0;
      for (const auto& entry : tfTable_.at(token)) {
        if (entry.sentenceNumber == sentenceNumber) {
          tf = entry.tf;
          if (!isEntryIndexSet) {
            isEntryIndexSet = true;
            tokenToPut = token;
            entryIndexForFirstToken = index;
          }
          break;
        }
        index++;
      }

      weight += (tf * idf);
    }

    weight /= penaltyScore;

    return SentenceWeighingResult(tokenToPut, weight, entryIndexForFirstToken, sentenceNumber);
  }

  wstring
  Snippeter::
  getStringSnippet(const vector<Snippeter::SentenceWeighingResult>& results) const
  {
    wstring snippet = getSentence(results.at(0));

    for (uint64_t i = 1; i < results.size(); ++i) {
      snippet += L" ... " + getSentence(results.at(i));
    }

    return snippet;
  }

  std::wstring
  Snippeter::
  getSentence(const SentenceWeighingResult& weighingResult) const
  {
    size_t sentenceNumber = tfTable_
            .at(weighingResult.term)[weighingResult.tfTableEntryIndex]
            .sentenceNumber;
    size_t start = offsetTable_[sentenceNumber];
    size_t end;
    try {
      end = offsetTable_.at(sentenceNumber + 1);
    } catch (std::out_of_range& e) {
      end = searchDocSize_;
    }

    return std::wstring(searchDoc_ + start, end - start);
  }

  Snippeter::uint64_t
  Snippeter::
  getSentenceLength(sentence_number_t sentenceNumber) const
  {
    size_t start = offsetTable_[sentenceNumber];
    size_t end;
    try {
      end = offsetTable_.at(sentenceNumber + 1);
    } catch (std::out_of_range& e) {
      end = searchDocSize_;
    }

    return end - start;
  }

  void
  Snippeter::
  sortSentencesByWeight(std::vector<Snippeter::SentenceWeighingResult>& weighedSentences) const
  {
    auto cmpByWeight = [](const SentenceWeighingResult& res1, const SentenceWeighingResult& res2) ->int {
        return res1.weight > res2.weight;
    };
    sort(weighedSentences.begin(), weighedSentences.end(), cmpByWeight);
  }

  void
  Snippeter::
  sortSentencesByIndex(std::vector<Snippeter::SentenceWeighingResult>& weighedSentences) const {
    auto cmpByIndex = [](const SentenceWeighingResult& res1, const SentenceWeighingResult& res2) ->int {
        return res1.documentSentenceNumber < res2.documentSentenceNumber;
    };
    sort(weighedSentences.begin(), weighedSentences.end(), cmpByIndex);
  }

}
