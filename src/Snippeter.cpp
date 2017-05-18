#include "Snippeter.h"

namespace gmsnippet {

  Snippeter::
  Snippeter(const std::string& filepath)
  {
    if (loadSearchDocument(filepath)) {
      parseSearchDocument();
    }
  }

  Snippeter::
  ~Snippeter()
  {
    if (searchDoc_.get() != nullptr) {
      munmap(searchDoc_.get(), searchDocSize_ * sizeof(wchar_t));
    }
  }

  bool
  Snippeter::
  loadSearchDocument(const std::string& filepath)
  {
    FILE* searchFile = fopen(filepath.c_str(), "r");

    if (searchFile == nullptr) {
      std::wcout << L"Не удалось открыть файл" << std::endl;
      perror("Error");
      return false;
    }

    int filed = fileno(searchFile);
    long seekResult = lseek(filed, 0, SEEK_END);
    if (seekResult == -1) {
      std::wcout << L"Не удалось подсчитать длину файла" << std::endl;
      perror("Error");
      return false;
    }

    searchDocSize_ = (size_t) seekResult;

    char* file = (char*) mmap(nullptr, searchDocSize_, PROT_READ, MAP_PRIVATE, filed, 0);
    fclose(searchFile);

    if (file == MAP_FAILED) {
      perror("Error");
      std::wcout << L"Не удалось загрузить файл" << std::endl;
      return false;
    }

    searchDoc_.reset(new wchar_t[searchDocSize_]);

    if (mbstowcs(searchDoc_.get(), file, searchDocSize_) == (size_t)-1) {
      std::wcout << L"Не удалось обработки текста" << std::endl;
      munmap(file, searchDocSize_);
      perror("Error");
      return false;
    }

    if (munmap(file, searchDocSize_) == -1) {
      perror("Error");
      std::wcout << L"Не удалось освободить файл" << std::endl;
      return false;
    }

    return true;
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

      current = searchDoc_.get()[pos];
      if ((current == L'\n' && previous == L'\n') ||
          current == L'.' || current == L'?'|| current ==  L'!') {
        setTfIdf(wordStart, pos, sentenceNumber);
        offsetTable_.push_back(++pos);
        sentenceNumber++;
        while(!iswalnum((wint_t)searchDoc_.get()[pos])){ pos++; }
        offsetTable_.push_back(pos);
        wordStart = pos;
        sentenceNumber++;
      }
      else if (current == L' ' || current == L',') {
        setTfIdf(wordStart, pos, sentenceNumber);
        wordStart = pos + 1;
      }
      previous = current;

    }

    // if text had no sentence finishing punctuation marks
    if (sentenceNumber == 0) {
      setTfIdf(wordStart, searchDocSize_, sentenceNumber);
      offsetTable_.push_back(searchDocSize_);
    }

  }

  void
  Snippeter::
  setTfIdf(size_t wordStart, size_t wordEnd, size_t sentenceNumber)
  {
    std::wstring word(searchDoc_.get() + wordStart, wordEnd - wordStart);

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
      if (TextUtils::isalnum(word) && idfTable_.count(word) > 0) {
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
    try {
      if (weighingResult.term == L"") {
        return L"";
      }
      size_t sentenceNumber = tfTable_
              .at(weighingResult.term)[weighingResult.tfTableEntryIndex]
              .sentenceNumber;
      size_t start = offsetTable_[sentenceNumber];
      size_t end;
      try {
        end = offsetTable_[sentenceNumber + 1];
      } catch (std::out_of_range& e) {
        end = searchDocSize_;
      }

      return std::wstring(searchDoc_.get() + start, end - start);
    } catch (std::out_of_range& e) {

      return L"";
    }
  }

}