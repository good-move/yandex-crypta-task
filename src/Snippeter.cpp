#include "Snippeter.h"

namespace gmsnippet {

  Snippeter::~Snippeter()
  {
    delete[] searchDoc;
  }

  void
  Snippeter::
  initSnippeter(const string& filepath)
  {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    loadSearchDocument(filepath);
    parseSearchDocument();
  }

  void Snippeter::loadSearchDocument(const string& filepath)
  {
    FILE* searchFile = fopen(filepath.c_str(), "r");

    if (searchFile == nullptr) {
      wcout << L"Не удалось открыть файл" << endl;
      perror("Error");
      return;
    }

    int filed = fileno(searchFile);
    long seekResult = lseek(filed, 0, SEEK_END);
    if (seekResult == -1) {
      wcout << L"Не удалось подсчитать длину файла" << endl;
      perror("Error");
      return;
    }

    this->searchDocSize = (size_t) seekResult;

    char* file = (char*) mmap(nullptr, this->searchDocSize, PROT_READ, MAP_PRIVATE, filed, 0);
    fclose(searchFile);

    if (file == MAP_FAILED) {
      perror("Error");
      wcout << L"Не удалось загрузить файл" << endl;
      throw runtime_error("Failed to map document " + filepath);
    }

    this->searchDoc = new wchar_t[this->searchDocSize * sizeof(wchar_t)];

    if (mbstowcs(this->searchDoc, file, this->searchDocSize) == (size_t)-1) {
      wcout << "Не удалось обработки текста" << endl;
      munmap(file, this->searchDocSize);
      perror("Error");
    }

    munmap(file, this->searchDocSize);
  }

  void Snippeter::parseSearchDocument()
  {
    wchar_t current;
    wchar_t previous = L'.';

    size_t sentenceNumber = 0;
    size_t wordStart = 0;

    this->offsetTable.push_back(0);
    for (size_t pos = 0; pos < this->searchDocSize ; ++pos) {

      current = this->searchDoc[pos];
      if (current == L'\n' && previous == L'\n' ||
          current == L'.' || current == L'?'|| current ==  L'!') {
        setTfIdf(wordStart, pos, sentenceNumber);
        this->offsetTable.push_back(++pos);
        sentenceNumber++;
        while(!iswalnum((wint_t)searchDoc[pos])){ pos++; }
        this->offsetTable.push_back(pos);
        wordStart = pos;
        sentenceNumber++;
      }
      else if (current == L' ' || current == L',') {
        setTfIdf(wordStart, pos, sentenceNumber);
        wordStart = pos + 1;
      }
      previous = current;

    }
  }

  void
  Snippeter::
  setTfIdf(size_t wordStart, size_t wordEnd, size_t sentenceNumber)
  {
    wstring word(searchDoc + wordStart, wordEnd - wordStart);

    if (!TextUtils::isalnum(word)) {
      return;
    }

    // unify all terms presentation
    TextUtils::lowercase(word);
    word = TextUtils::trim(word);

    // add up global term frequency
    try {
      idfTable.at(word) += 1;
    } catch (out_of_range& e) {
      this->idfTable[word] = 1;
    }

    // register term frequency for currently processed sentence
    try {
      auto& sentences = tfTable.at(word);
      bool found = false;
      for (auto& sentence : sentences) {
        if (sentence.sentenceNumber == sentenceNumber) {
          found = true;
          sentence.tf++;
        }
      }
      if (!found) {
        TFTableEntry entry{sentenceNumber, 1};
        sentences.push_back(entry);
      }

//       the term has either been met in current sentence or not
//      if (sentences.back().sentenceNumber == sentenceNumber) {
//        sentences.back().tf++;
//      } else {
//        TFTableEntry entry{sentenceNumber, 1};
//        sentences.push_back(entry);
//      }
    } catch (out_of_range& e) {
      TFTableEntry entry{sentenceNumber, 1};
      tfTable[word].push_back(entry);
    }
  }

  wstring
  Snippeter::
  getSnippet(wstring query)
  {
    vector<wstring> queryWords = getWordsFromQuery(query);
    if (queryWords.empty()) {
      return L"По вашему запросу ничего не найдено";
    }

    return getBestSnippet(queryWords);
  }

  vector<wstring>
  Snippeter::
  getWordsFromQuery(wstring &query) const
  {
    #ifdef DEBUG
    wprintf(L"Entered %s\n", __FUNCTION__);
    #endif

    TextUtils::lowercase(query);
    wstringstream ss(query);
    if (ss.fail()) {
      perror("Error");
      throw runtime_error("Couldn't parse query\n");
    }

    // Split query into words, filter unknown ones
    // and sort remaining by idf in descending order
    vector<wstring> queryWords;
    while(!ss.eof()) {
      wstring word;
      ss >> word;
      if (TextUtils::isalnum(word) && idfTable.count(word) > 0) {
        queryWords.push_back(word);
      }
    }

    auto idfWordCmp = [this](const wstring& s1, const wstring& s2) -> bool {
      return this->idfTable.at(s1) < this->idfTable.at(s2);
    };

    sort(queryWords.begin(), queryWords.end(), idfWordCmp);

    #ifdef DEBUG
    wprintf(L"Exiting %s\n", __FUNCTION__);
    #endif

    return queryWords;
  }

  wstring
  Snippeter::
  getBestSnippet(vector<wstring>& queryWords) const
  {

    #ifdef DEBUG
    wprintf(L"Entered %s\n", __FUNCTION__);
    #endif

    const auto& matches1 = getMaxWeightSentences(queryWords, 0);
    const auto& matches2 = getMaxWeightSentences(queryWords, 1);

    #ifdef DEBUG
      wprintf(L"Exiting 2 %s\n", __FUNCTION__);
    #endif

    if (matches2.empty()) {
      return getSentence(matches1[0]);
    }

    return getSentence(matches1[0]) + L"\n" + getSentence(matches2[0]);
  }

  vector<Snippeter::SentenceWeighingResult>
  Snippeter::
  getMaxWeightSentences(const vector<wstring>& queryWords, size_t startWordIndex) const {

    size_t maxMatchesAllowed = 2;

    vector<SentenceWeighingResult> weighingResults;
    // maxMatch1 is intended to always have bigger or
    // equal weight to maxMatch2
    SentenceWeighingResult maxMatch1, maxMatch2;
    double weightThreshold = 0;

    #ifdef DEBUG
    wprintf(L"Entered %s\n", __FUNCTION__);
    #endif

    if (startWordIndex >= queryWords.size()) {
      return weighingResults;
    }

    const auto& strongerTerm = queryWords.at(startWordIndex);
    const double strongerTermIDF = idfTable.at(strongerTerm);
    size_t tfTableEntryIndex = 0;
    for (const auto& sentenceInfo : tfTable.at(strongerTerm)) {

      double weight = sentenceInfo.tf / strongerTermIDF;

      // now find intersects with other words and sum upp their weight

      size_t weakTermIndex = startWordIndex + 1;
      size_t matches = 0;
      while (weakTermIndex < queryWords.size() && matches < maxMatchesAllowed) {
        const auto& weakerTerm = queryWords[weakTermIndex];
        size_t tf = getLowerTermTF(weakerTerm, sentenceInfo.sentenceNumber);
        weight += tf / idfTable.at(weakerTerm);
        matches += (tf > 0);
        weakTermIndex++;
      }

      if (weight > weightThreshold) {
        if (weight > maxMatch1.weight) {
          maxMatch2 = maxMatch1;
          maxMatch1 = SentenceWeighingResult{strongerTerm, weight, tfTableEntryIndex, sentenceInfo.sentenceNumber};
        } else if (weight > maxMatch2.weight) {
          maxMatch1 = SentenceWeighingResult{strongerTerm, weight, tfTableEntryIndex, sentenceInfo.sentenceNumber};
        }
        weightThreshold = maxMatch2.weight;
      }

      tfTableEntryIndex++;
    }


    #ifdef DEBUG
    wprintf(L"Exiting %s\n", __FUNCTION__);
    #endif

    weighingResults.push_back(maxMatch1);
    weighingResults.push_back(maxMatch2);

    return weighingResults;

  }

  size_t
  Snippeter::
  getLowerTermTF(const wstring &word, size_t sentenceNum) const
  {
    #ifdef DEBUG
    wprintf(L"Entered %s\n", __FUNCTION__);
    #endif

    const auto& sentences = tfTable.at(word);

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

    #ifdef DEBUG
    wprintf(L"Exiting %s\n", __FUNCTION__);
    #endif

    return (sentences[start].sentenceNumber == sentenceNum) * sentences[start].tf;
  }

  wstring
  Snippeter::
  getSentence(const SentenceWeighingResult& weighingResult) const
  {
    #ifdef DEBUG
    wprintf(L"Entered %s\n", __FUNCTION__);
    #endif

    try {
      if (weighingResult.term == L"") {
        wcout << "WARNING" << endl;
      }
      size_t sentenceNumber = tfTable
              .at(weighingResult.term)[weighingResult.tfTableEntryIndex]
              .sentenceNumber;
      size_t start = offsetTable[sentenceNumber];
      size_t end = offsetTable[sentenceNumber + 1];

      #ifdef DEBUG
      wprintf(L"Exiting %s\n", __FUNCTION__);
      #endif

      return wstring(searchDoc + start, end - start);
    } catch (out_of_range& e) {

      #ifdef DEBUG
      wprintf(L"Exiting %s\n", __FUNCTION__);
      #endif

      return L"";
    }
  }

}