INC_DIRS := ../utils
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g -I$(INC_DIRS)
TARGET := sample_server
SRCS := $(wildcard *.cpp *.c) $(wildcard ../utils/*.cpp ../utils/*.c)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET)
