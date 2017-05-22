#include <sys/stat.h>
#include "Snippeter.h"

namespace gmsnippet {

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
    FILE* searchFile = fopen(filepath.c_str(), "r");

    if (searchFile == nullptr) {
      perror("Error");
      throw std::runtime_error("Failed to open file");
    }

    int filed = fileno(searchFile);
    long seekResult = lseek(filed, 0, SEEK_END);
    if (seekResult == -1) {
      perror("Error");
      throw std::runtime_error("Failed to get file length");
    }

    searchDocSize_ = (size_t) seekResult;

    char* file = (char*) mmap(nullptr, searchDocSize_, PROT_READ, MAP_PRIVATE, filed, 0);
    fclose(searchFile);

    if (file == MAP_FAILED) {
      perror("Error");
      throw std::runtime_error("Failed to load file into memory");
    }

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
      idfTable_.at(word) += 1;
    } catch (std::out_of_range& e) {
      idfTable_[word] = 1;
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
    std::vector<std::wstring> queryWords = getWordsFromQuery(query);
    if (queryWords.empty()) {
      return L"По вашему запросу ничего не найдено";
    }

    return getBestSnippet(queryWords);
  }

  std::vector<std::wstring>
  Snippeter::
  getWordsFromQuery(std::wstring& query) const
  {
    TextUtils::lowercase(query);
    std::wstringstream ss(query);
    if (ss.fail()) {
      perror("Error");
      throw std::runtime_error("Couldn't parse query\n");
    }

    // Split query into words, filter unknown ones
    // and sort remaining by idf in descending order
    std::vector<std::wstring> queryWords;
    while(!ss.eof()) {
      std::wstring word;
      ss >> word;
      word = TextUtils::trim(word);
      if (word.length() > 0 && TextUtils::isalnum(word) && idfTable_.count(word) > 0) {
        queryWords.push_back(word);
      }
    }

    auto idfWordCmp = [this](const std::wstring& s1, const std::wstring& s2) -> bool {
      return this->idfTable_.at(s1) < this->idfTable_.at(s2);
    };

    sort(queryWords.begin(), queryWords.end(), idfWordCmp);

    return queryWords;
  }

  std::wstring
  Snippeter::
  getBestSnippet(std::vector<std::wstring>& queryWords) const
  {
    std::vector<SentenceWeighingResult> finalMatch(4);
    const auto& matches1 = getMaxWeightSentences(queryWords, 0);
    const auto& matches2 = getMaxWeightSentences(queryWords, 1);

    if (matches2.empty()) {
      if (matches1[1].term == L"") {
        return getSentence(matches1[0]);
      }
      return getSentence(matches1[0]) + L" ... " + getSentence(matches1[1]);
    }

    // merge sort two matches
    for (int k = 0, i = 0, j = 0; k < 4; k++) {
      if (i==2) {
        finalMatch[k] = matches2[j];
        j++;
      } else if (j==2) {
        finalMatch[k] = matches1[i];
        i++;
      } else {
        if (matches1[i].weight > matches2[j].weight) {
          finalMatch[k] = matches1[i];
          i++;
        } else {
          finalMatch[k] = matches2[i];
          j++;
        }
      }
    }

    if (finalMatch[1].term == L"") {
      return getSentence(finalMatch[0]);
    }

    return getSentence(finalMatch[0]) + L" ... " + getSentence(finalMatch[1]);
  }

  std::vector<Snippeter::SentenceWeighingResult>
  Snippeter::
  getMaxWeightSentences(const std::vector<std::wstring>& queryWords, size_t startWordIndex) const
  {
    size_t maxMatchesAllowed = 3;

    std::vector<SentenceWeighingResult> weighingResults;
    // maxMatch1 is intended to always have bigger or
    // equal weight to maxMatch2
    SentenceWeighingResult maxMatch1, maxMatch2;
    double weightThreshold = 0;

    if (startWordIndex >= queryWords.size()) {
      return weighingResults;
    }

    const auto& strongerTerm = queryWords.at(startWordIndex);
    const double strongerTermIDF = idfTable_.at(strongerTerm);
    const size_t queryWordsSize = queryWords.size();
    size_t tfTableEntryIndex = 0;
    for (const auto& sentenceInfo : tfTable_.at(strongerTerm)) {

      double weight = sentenceInfo.tf / strongerTermIDF;

      // find intersects with other words and sum upp their weight

      size_t weakTermIndex = startWordIndex + 1;
      size_t matches = 0;
      while (weakTermIndex < queryWordsSize && matches < maxMatchesAllowed) {
        const auto& weakerTerm = queryWords[weakTermIndex];
        size_t tf = getLowerTermTF(weakerTerm, sentenceInfo.sentenceNumber);
        weight += tf / idfTable_.at(weakerTerm);
        matches += (tf > 0);
        weakTermIndex++;
      }

      if (weight > weightThreshold) {
        if (weight > maxMatch1.weight) {
          maxMatch2 = maxMatch1;
          maxMatch1 = SentenceWeighingResult{strongerTerm, weight, tfTableEntryIndex, sentenceInfo.sentenceNumber};
        } else if (weight > maxMatch2.weight) {
          maxMatch2 = SentenceWeighingResult{strongerTerm, weight, tfTableEntryIndex, sentenceInfo.sentenceNumber};
        }
        weightThreshold = maxMatch2.weight;
      }

      tfTableEntryIndex++;
    }

    weighingResults.push_back(maxMatch1);
    weighingResults.push_back(maxMatch2);

    return weighingResults;

  }

  size_t
  Snippeter::
  getLowerTermTF(const std::wstring& word, size_t sentenceNum) const
  {
    const auto& sentences = tfTable_.at(word);

    if (sentences.front().sentenceNumber > sentenceNum ||
        sentences.back().sentenceNumber < sentenceNum) {
      return 0;
    }

    size_t start = 0;
    size_t end = sentences.size();
    size_t middle = 0;

    while(start < end) {
      middle = (start + end) / 2;
      if (sentenceNum <= sentences[middle].sentenceNumber) {
        end = middle;
      } else {
        start = middle + 1;
      }
    }

    return (sentences[start].sentenceNumber == sentenceNum) * sentences[start].tf;
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

}