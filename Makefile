# use compiler of your choice
CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

# .cpp files
SRCS = src/bfpp.cpp lib/Tokenizer.cpp

# .o
OBJS = $(SRCS:.cpp=.o)

# where to
TARGET = bin/bfpp

# include
INCLUDE = include

all: mkdir_bin $(TARGET)

mkdir_bin:
	mkdir -p bin


$(TARGET): $(OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I$(INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean