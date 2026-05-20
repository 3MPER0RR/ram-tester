# ram-tester 

Ram hardware info

## Code compiler

## Debian 

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o ramtest main.cpp

## For debug

g++ -std=c++17 -O0 -g -Wall -Wextra -pedantic -o ramtest main.cpp

## Windows 

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o ramtest.exe main.cpp


## Usage Examples

./ramtest --inventory

./ramtest --test --mb <MB> --passes <N> [--pattern <aa|55|00|ff>] [--module <index>]

./ramtest --test --mb 8 --passes 1 --pattern aa

./ramtest --test --mb 32 --passes 2 --pattern ff

./ramtest --test --mb 16 --passes 1 --pattern 55 --module 1

