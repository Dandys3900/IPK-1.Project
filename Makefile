CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra
SRCS := main.cpp UDPClass.cpp TCPClass.cpp
OBJS := $(SRCS:.cpp=.o)
EXE := ipk24chat-client

$(EXE): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(EXE)

clean:
	rm -f $(EXE)

.PHONY: all clean
