FLAGS= -std=c++11 -Wall -Werror -O3

all:
	g++ $(FLAGS) src/Snippeter.cpp main.cpp -o snippeter
