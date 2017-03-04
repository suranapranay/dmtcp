FILE=simpleIrecv
RFILE=grid1
default: build

$(FILE).o: $(FILE).c
	mpicc -g3 -O0 -c $(FILE).c -o $(FILE).o

$(FILE): $(FILE).o
	mpicc -g3 -O0 $(FILE).o -o $(FILE) -lm



$(RFILE).o: $(RFILE).c
	mpicc -g3 -O0 -c $(RFILE).c -o $(RFILE).o

$(RFILE): $(RFILE).o
	mpicc -g3 -O0 $(RFILE).o -o $(RFILE) -lm



vi: $(FILE).c
	vim $(FILE).c

build: $(FILE)  $(RFILE)

run: build
	mpirun -n 4 -hosts localhost ./$(FILE) 2 1 10

gridrun: build
	mpirun -n 4 -hosts localhost ./$(RFILE) 2 1 10
clean:
	rm -rf *.o *.swp $(FILE) $(RFILE)
