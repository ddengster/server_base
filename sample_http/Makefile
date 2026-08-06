INC_DIRS := ../utils
CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -g -I$(INC_DIRS)
LDFLAGS := -luv -ljwt
TARGET := sample_http
SRCS := $(wildcard *.cpp *.c) $(wildcard ../utils/*.cpp ../utils/*.c)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET)
