Huffman:Huffman.cpp
	g++ -o Huffman.o Huffman.cpp
	./Huffman.o

LZ:LZ.cpp
	g++ -o LZ.o LZ.cpp
	./LZ.o

Arithmetic:Arithmetic.cpp
	g++ -o Arithmetic.o Arithmetic.cpp -lgmp -lgmpxx
	./Arithmetic.o

.PHONY: clean
clean:
	rm -f Huffman.o LZ.o Arithmetic.o