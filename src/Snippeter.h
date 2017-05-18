//
// Created by alexey on 14.05.17.
//

#ifndef KRIPTA_TASK_SNIPPETER_H
#define KRIPTA_TASK_SNIPPETER_H

#include <unordered_map>
#include <sys/mman.h>
#include <algorithm>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <locale>
#include <memory>

namespace gmsnippet {

  class Snippeter {

    public:

      // Initializes all main procedures for search document processing, namely:
      // counting tf-IDF for each term in text document and building document offset table
      // for faster sentences access
      Snippeter(const std::string& filepath);

      ~Snippeter();

      // A common interface to get a snippet for a search query
      std::wstring getSnippet(std::wstring query) const;

    private:

      // -------------------------------------------------------------------------
      //                     Text preprocessing routines

      // Opens search document and loads it into memory
      bool loadSearchDocument(const std::string& filepath);

      // Goes through search document and finds word and sentence boundaries
      void parseSearchDocument();

      // Cuts a word out of search document and registers its occurrences
      // according to tf-IDF concept
      void setTfIdf(size_t wordStart, size_t wordEnd, size_t sentenceNumber);


      // -------------------------------------------------------------------------
      //                     Snippet creation routines

      // Aggregates main info about sentence that is a strong
      // candidate to be included in a snippet
      struct SentenceWeighingResult;

      // An element of |tfTable_| property
      struct TFTableEntry;

      // Turns a search query string into a program-appropriate format
      std::vector<std::wstring> getWordsFromQuery(std::wstring& query) const;

      // This function is in charge of processing the index and formatted query
      // so that "the best" snippet is created
      std::wstring getBestSnippet(std::vector<std::wstring>& queryWords) const;

      // Calculates weights for sentences that match search query words
      std::vector<SentenceWeighingResult>
      getMaxWeightSentences(const std::vector<std::wstring>& queryWords, size_t startWordIndex) const;

      // Checks whether a |word| is met in |sentenceNum| and returns its TF on success
      size_t getLowerTermTF(const std::wstring &word, size_t sentenceNum) const;

      // Finalizes snippet creation process
      // Returns a sentence that is to be present in the snippet
      std::wstring getSentence(const SentenceWeighingResult&) const;

      // -------------------------------------------------------------------------
      //                            Class State

      std::unordered_map<std::wstring, std::vector<TFTableEntry>> tfTable_;
      std::unordered_map<std::wstring, size_t> idfTable_;
      std::unique_ptr<wchar_t> searchDoc_{nullptr};
      std::vector<size_t> offsetTable_;
      size_t searchDocSize_;

      // -------------------------------------------------------------------------
      //                          Auxiliary classes

      struct TFTableEntry {

          // Sentence number in the search document.
          // Figured out within |init| method
          size_t sentenceNumber;

          // Term Frequency in |sentenceNumber|'th sentence
          size_t tf;

          TFTableEntry(size_t sentenceNumber, size_t tf) :
                   sentenceNumber(sentenceNumber), tf(tf) {};

      };

      // Contains main info about a sentence
      struct SentenceWeighingResult {

          // A term the sentence is linked to (aka contains)
          std::wstring term = L"";

          // The index of element in tfTable_ vector for the |term| argument
          size_t tfTableEntryIndex = 0;

          // The index of element in tfTable_ vector for the |term| argument
          size_t documentSentenceNumber = 0;

          // Sentence weight, which is computed inside |getBestSnippet()|
          double weight = 0;

          SentenceWeighingResult() {};

          SentenceWeighingResult(std::wstring term, double weight, size_t index, size_t sentenceNumber) :
                  term(term), tfTableEntryIndex(index), documentSentenceNumber(sentenceNumber), weight(weight) {};

          bool operator==(const SentenceWeighingResult& other) const {
            return other.weight == weight && other.term == term && other.tfTableEntryIndex == tfTableEntryIndex;
          }

          bool operator!=(const SentenceWeighingResult& other) const {
            return !(this->operator==(other));
          }

      };

      // A static class with routines to ease text processing
      class TextUtils {
        public:
          // Removes any whitespace both in the beginning and the end of a wstring
          static std::wstring trim(const std::wstring& str) {
            size_t first = str.find_first_not_of(' ');
            if (std::wstring::npos == first) {
              return str;
            }
            size_t last = str.find_last_not_of(' ');
            return str.substr(first, (last - first + 1));
          }

          // Checks if a wstring is alpha-numeric
          static bool isalnum(const std::wstring& str) {
            return std::all_of(str.begin(), str.end(), [](wchar_t letter){ return iswalnum((wint_t)letter); });
          }

          static void lowercase(std::wstring& str) {
            std::transform(str.begin(), str.end(), str.begin(), std::towlower);
          }

      };


  }; // End of class Snippeter

}
#endif //KRIPTA_TASK_SNIPPETER_H
