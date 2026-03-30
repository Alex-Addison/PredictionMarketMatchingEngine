# compiler settings
GFLAGS = -std=c++14 -Wall -g -Wextra
LDFLAGS = -lquickfix -lpthread

all: trader gateway engine

trader: trader.cpp
	g++ $(GFLAGS) -o trader trader.cpp $(LDFLAGS)

gateway: gateway.cpp
	g++ $(GFLAGS) -o gateway gateway.cpp $(LDFLAGS)

engine: engine.cpp
	g++ $(GFLAGS) -o engine engine.cpp $(LDFLAGS)

clean:
	rm -f trader gateway engine