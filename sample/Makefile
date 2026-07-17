CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17
TARGET := sample
SRCS := $(wildcard *.cpp *.c)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET)
