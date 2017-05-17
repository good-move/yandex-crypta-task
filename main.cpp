#include "Snippeter.h"
#include <sys/time.h>

using namespace gmsnippet;

int main()
{
  setlocale(LC_ALL, "ru_RU.UTF-8");

  Snippeter snippeter;
  snippeter.initSnippeter("./assets/тест.txt");

  while(true) {
    wstring data;
    getline(wcin, data);

    timeval tv_start;
    gettimeofday(&tv_start, 0);

    wstring snippet = snippeter.getSnippet(data);

    timeval tv_end;
    gettimeofday(&tv_end, 0);

    wcout << snippet << endl;
    wcout << "Snippet creation time (ms): " << (tv_end.tv_usec - tv_start.tv_usec) << endl;
  }

//  snippeter.getSnippet(data);
//  }

//  wstring s(L"   Привет  ");
//  s = snippeter.trim(s);


//  wstring data = L"ПривЕт";
//  transform(data.begin(), data.end(), data.begin(), std::towlower);
//
//  wcout << s << endl;

  return 0;
}