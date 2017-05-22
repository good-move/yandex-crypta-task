FLAGS= -std=c++11 -Wall -Werror

all:
	g++ $(FLAGS) src/Snippeter.cpp main.cpp -o snippeter
