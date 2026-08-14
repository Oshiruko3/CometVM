# COMET VM - ビルドとサンプル実行
# 使い方:
#   make          ... cometvm をビルド
#   make sample   ... 全サンプルを実行
#   make clean    ... ビルド成果物を削除

CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc

SRC = main.cpp src/comet.cpp src/casl_asm.cpp
OBJ = $(SRC:.cpp=.o)

cometvm: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

SAMPLES = $(wildcard ../samples/sample*.cas)

sample: cometvm
	@for s in $(SAMPLES); do \
		echo "=== $$s ==="; \
		./cometvm $$s; \
	done

clean:
	rm -f cometvm $(OBJ)

.PHONY: sample clean
