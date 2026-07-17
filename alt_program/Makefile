CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g
TARGET := alt_program
SRCS := $(wildcard *.cpp *.c)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET)
