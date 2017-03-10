#!/usr/bin/python
import os
import sys
if __name__ == "__main__":
	print sys.argv
	if (sys.argv[1]) == 'd':
		os.system("../../../bin/dmtcp_coordinator --daemon")
	if (sys.argv[1]) == 'c':
		os.system("../../../bin/dmtcp_command --c")

	if (sys.argv[1]) == 'q':
		os.system("../../../bin/dmtcp_command -q")

	if (sys.argv[1]) == 'k':
		os.system("../../../bin/dmtcp_command -k")

	if (sys.argv[1]) == "launch":
		os.system("../../../bin/dmtcp_launch -j -p 7779 --with-plugin $PWD/libdmtcp_testwrapperI.so  mpirun -n 2  -hosts localhost tests/simpleIrecv &")

	if (sys.argv[1]) == "glaunch":
		os.system("../../../bin/dmtcp_launch -j -p 7779 --with-plugin $PWD/libdmtcp_testwrapperI.so  mpirun -n 2 -hosts localhost tests/grid1 2 10 &")



	if (sys.argv[1]) == "record":
		os.system("eval DMTCP_MPI_START_RECORD=1   ../../../bin/dmtcp_restart -j -p 7779 ckpt_*" )

	if (sys.argv[1]) == "replay":
		os.system("eval DMTCP_MPI_START_REPLAY=1   ../../../bin/dmtcp_restart -j -p 7779 ckpt_*" )


	if (sys.argv[1]) == "sreplay":
		os.system("eval DMTCP_SKIP_REFILL=1 DMTCP_MPI_START_REPLAY=1   ../../../bin/dmtcp_restart -j -p 7779 ckpt_grid1_1d4a852a5f139a6-47000-58c32bb6.dmtcp" )
