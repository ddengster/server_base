INC_DIRS := ../utils
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g -I$(INC_DIRS) -O2 -Og
LDFLAGS := -luv -ljwt
TARGET := alt_program
SRCS := $(wildcard *.cpp *.c) $(wildcard ../utils/*.cpp ../utils/*.c)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET)
