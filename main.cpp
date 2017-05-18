#include "src/Snippeter.h"
#include <sys/time.h>

using namespace gmsnippet;
using namespace std;

int main(int argc, char* argv[])
{
  setlocale(LC_ALL, "ru_RU.UTF-8");

  if (argc < 2) {
    wcout << L"Не указано имя файла, по которому производится поиск." << endl <<
             L"Использование:" << endl <<
             L"1) less <файл с запросами> | snippeter <имя файла>" << endl <<
             L"2) запустите `main <имя файла>` и вводите запросы по одному в строке" << endl;
    return 0;
  }

  try {
    Snippeter snippeter(argv[1]);
    for (wstring data; getline(wcin, data);) {

      wcout << endl << L"Запрос: " << data << endl;
      timeval tv_start;
      gettimeofday(&tv_start, 0);

      wstring snippet = snippeter.getSnippet(data);

      timeval tv_end;
      gettimeofday(&tv_end, 0);

      wcout << snippet << endl;
      wcout << L"Время формирования сниппета (мс): "
            << (tv_end.tv_usec - tv_start.tv_usec) << endl;
    }
  } catch(exception& e) {
    wcout << e.what() << endl;
  }

  return 0;
}