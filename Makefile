## you have command.cc command.h tokenizer.cc tokenizer.h you need compile them and link
## them to one object file called  : "myshell" 
## so we should run make , that will create our object myshell ,
## ./myshell should run your application 

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic -g
TARGET = myshell
SOURCES = command.cc tokenizer.cc

all: $(TARGET)

$(TARGET): $(SOURCES) command.h tokenizer.h
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET) myshell.log out output.txt out2

.PHONY: all clean
