INC_DIRS := ../utils
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g -I$(INC_DIRS) -O2 -Og
LDFLAGS := -luv -ljwt -lpq
TARGET := auth_server
SRCS := $(wildcard *.cpp *.c) $(shell find ../utils -type f \( -name '*.cpp' -o -name '*.c' \))

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET)
