//
// Created by alexey on 14.05.17.
//

#ifndef KRIPTA_TASK_SNIPPETER_H
#define KRIPTA_TASK_SNIPPETER_H

#include <unordered_map>
#include <sys/mman.h>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <locale>
#include <algorithm>
#include <codecvt>
#include <fcntl.h>

//#define DEBUG 1

using namespace std;

namespace gmsnippet {

  class Snippeter {

    public:

      ~Snippeter();

      // Initializes all main procedures for search document processing, namely:
      // counting tf-IDF for each term in text document and building document offset table
      // for faster sentences access
      void initSnippeter(const string& filepath);

      // A common interface to get a snippet for a search query
      wstring getSnippet(wstring query);

    private:

      // -------------------------------------------------------------------------
      //                     Text preprocessing routines

      // Opens search document and loads it into memory
      void loadSearchDocument(const string& filepath);


      void parseSearchDocument();

      // Cuts a word out of search document and registers its occurrences
      // according to tf-IDF concept
      void setTfIdf(size_t wordStart, size_t wordEnd, size_t sentenceNumber);


      // -------------------------------------------------------------------------
      //                     Snippet creation routines

      // Aggregates main info about sentence that is a strong
      // candidate to be included in a snippet
      struct SentenceWeighingResult;

      // An element of |tfTable| property
      struct TFTableEntry;

      // Turns a search query string into a program-appropriate format
      vector<wstring> getWordsFromQuery(wstring& query) const;

      // This function is in charge of processing the index and formatted query
      // so that "the best" snippet is created
      wstring getBestSnippet(vector<wstring>& queryWords) const;

      // Calculates weights for sentences that match search query words
      vector<SentenceWeighingResult> getMaxWeightSentences(const vector<wstring>& queryWords, size_t startWordIndex) const;

      // Checks whether a |word| is met in |sentenceNum|
      size_t getLowerTermTF(const wstring &word, size_t sentenceNum) const;

      // Finalizes snippet creation process
      // Returns a sentence that is to be present in the snippet
      wstring getSentence(const SentenceWeighingResult&) const;

      // -------------------------------------------------------------------------
      //                       Class State

      unordered_map<wstring, vector<TFTableEntry>> tfTable; // term -> < (sentenceNumber, tf), ... >
      unordered_map<wstring, size_t> idfTable;
      wchar_t* searchDoc = nullptr;
      vector<size_t> offsetTable;
      size_t searchDocSize;

      // -------------------------------------------------------------------------
      //                       Auxiliary classes


      struct TFTableEntry {

          // Sentence number in the search document.
          // Figured out within |initSnippeter| method
          size_t sentenceNumber;

          // Term Frequency in |sentenceNumber|'th sentence
          size_t tf;

          explicit TFTableEntry(size_t sentenceNumber, size_t tf) :
                   sentenceNumber(sentenceNumber), tf(tf) {};

      };

      // Contains main info about a sentence
      struct SentenceWeighingResult {

          // A term the sentence is linked to (aka contains)
          wstring term = L"";

          // The index of element in tfTable vector for the |term| argument
          size_t tfTableEntryIndex = 0;

          // The index of element in tfTable vector for the |term| argument
          size_t documentSentenceNumber = 0;

          // Sentence weight, which is computed inside |getBestSnippet()|
          double weight = 0;

          SentenceWeighingResult() {};

          SentenceWeighingResult(wstring term, double weight, size_t index, size_t sentenceNumber) :
                  term(term), tfTableEntryIndex(index), weight(weight), documentSentenceNumber(sentenceNumber) {};

          bool operator==(const SentenceWeighingResult& other) {
            return other.weight == weight && other.term == term && other.tfTableEntryIndex == tfTableEntryIndex;
          }

          bool operator!=(const SentenceWeighingResult& other) {
            return !(this->operator==(other));
          }

      };



      // A static class with routines to ease text processing
      class TextUtils {
        public:
          // Removes any whitespace both in the beginning and the end of a wstring
          static wstring trim(const wstring& str) {
            size_t first = str.find_first_not_of(' ');
            if (string::npos == first) {
              return str;
            }
            size_t last = str.find_last_not_of(' ');
            return str.substr(first, (last - first + 1));
          }

          // Checks if a wstring is alpha-numeric
          static bool isalnum(const wstring& str) {
            return all_of(str.begin(), str.end(), [](wchar_t letter){ return iswalnum((wint_t)letter); });
          }

          static void lowercase(wstring& str) {
            transform(str.begin(), str.end(), str.begin(), std::towlower);
          }

      };


  }; // End of class Snippeter

}
#endif //KRIPTA_TASK_SNIPPETER_H
