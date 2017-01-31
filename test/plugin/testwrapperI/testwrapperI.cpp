#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <mpi.h>

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "util.h"

#define ENV_MPI_RECORD  "DMTCP_MPI_START_RECORD"
#define ENV_MPI_REPLAY  "DMTCP_MPI_START_REPLAY"

#define MPI_WRAPPER_GDB 5
// from dmtcpplugin.cpp
#define SUCCESS             0
#define NOTFOUND            -1
#define TOOLONG             -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR      -4
#define NULL_PTR            -5
#define MPIIRECV            1
#define MPIWAIT             2

struct _MPIIrecvSignature{
 int count;
 MPI_Datatype mDtype;
 int src;
 int tag;
 MPI_Comm comm;
 MPI_Request request;
 int result;
 char *buffer;
};

typedef struct _MPIIrecvSignature MPI_IRecvCallSign;

struct _MPIWaitSignature{
    MPI_Request request;
    MPI_Status status;
    int result;
};

typedef struct _MPIWaitSignature MPI_WaitSign;

struct MPI_UIRecvWait{
    unsigned int callType;
    void *callbuffer;
};

enum RUNNING_MODE
{
  NONE,
  RECORD,
  REPLAY,
};

static RUNNING_MODE currMode = NONE;
static FILE* logFile = NULL;

static void
errCheckGetRestartEnv(int ret)
{
    /* ret == -1 is fine; everything else is not */
    if (ret < -1 /* RESTART_ENV_NOT_FOUND */) {
        JASSERT(ret != TOOLONG).Text("mpiwrapper: DMTCP_PATH_PREFIX exceeds "
                "maximum size (10kb). Use a shorter environment variable "
                "or increase MAX_ENV_VAR_SIZE and recompile.");

        JASSERT(ret != DMTCP_BUF_TOO_SMALL).Text("dmtcpplugin: DMTCP_PATH_PREFIX exceeds "
                "dmtcp_get_restart_env()'s MAXSIZE. Use a shorter "
                "environment variable or increase MAXSIZE and recompile.");

        /* all other errors */
        JASSERT(ret >= 0).Text("Fatal error retrieving DMTCP_PATH_PREFIX "
                "environment variable.");
    }
}

static void
recordData(MPI_UIRecvWait &mpiRecvWait)
{
  JASSERT(logFile);
  JASSERT(currMode == RECORD);
  int callType = mpiRecvWait.callType;
  int sz;
  int datasz;
  if(callType == MPIIRECV){
      sz = sizeof(MPI_IRecvCallSign);
      MPI_IRecvCallSign mpiIRecvData = *(MPI_IRecvCallSign*) mpiRecvWait.callbuffer;
      datasz = mpiIRecvData.count * mpiIRecvData.mDtype;
      fwrite(&mpiRecvWait, sizeof(MPI_UIRecvWait), 1, logFile);
      fwrite(mpiRecvWait.callbuffer, sz, 1, logFile);
      fwrite(mpiIRecvData.buffer, datasz, 1, logFile);
  }
  else{
      fwrite(&mpiRecvWait, sizeof(MPI_UIRecvWait), 1, logFile);
      fwrite(mpiRecvWait.callbuffer, sizeof(MPI_WaitSign), 1, logFile);
  }

  JTRACE("Recorded data");
}

static void replayData(MPI_UIRecvWait& _replayData)
{
  JASSERT(logFile);
  JASSERT(currMode == REPLAY);

  // -1 for the pointer to buffer
  fread(&_replayData, sizeof(MPI_UIRecvWait), 1, logFile);
  if(_replayData.callType == MPIWAIT){
      //MPI WAIT To Replay
    MPI_WaitSign *wait = new MPI_WaitSign();
    fread(wait,sizeof(MPI_WaitSign), 1, logFile);
    _replayData.callbuffer = wait;
  }
  else{
    MPI_IRecvCallSign *irecv = new MPI_IRecvCallSign();
    fread(irecv, sizeof(MPI_IRecvCallSign), 1, logFile);
    irecv -> buffer = (char*)malloc(sizeof(irecv -> mDtype) * irecv -> count);
    fread(irecv -> buffer, sizeof(irecv -> mDtype) * irecv -> count, 1, logFile);
    _replayData.callbuffer = irecv;
  }
  return;

}

int MPI_Wait(MPI_Request *request, MPI_Status *status)
{
  int result = 0;
  if (currMode == RECORD) {
    result = NEXT_FNC(MPI_Wait)(request, status);
    MPI_UIRecvWait commonDt = {
        .callType = MPIWAIT,
        .callbuffer = NULL
    };

    recordData(commonDt);
    goto done;
  }
  else if (currMode == REPLAY) {
    MPI_UIRecvWait commonDt = {0};
    commonDt.callType = MPIWAIT;
    replayData(commonDt);
    MPI_WaitSign mpiSign = *(MPI_WaitSign*)(commonDt.callbuffer);
    request = &(mpiSign.request);
    status = &(mpiSign.status);
    result = mpiSign.result;
    goto done;
  }

  result = NEXT_FNC(MPI_Wait)(request, status);
done:
  return result;
}



int MPI_Irecv(void *buf, int count, MPI_Datatype datatype,
                int source, int tag, MPI_Comm comm, MPI_Request *request)
{
  int result = 0;
  if (currMode == RECORD) {
    result = NEXT_FNC(MPI_Irecv)(buf, count, datatype, source, tag, comm, request);
    MPI_IRecvCallSign mpiSign = { .count = count,
        .mDtype = datatype,
        .src = source,
        .tag = tag,
        .comm = comm,
        .request = *request,
        .result = result,
        .buffer = (char*)buf};

    MPI_UIRecvWait commonDt = {
        .callType = MPIIRECV,
        .callbuffer = NULL
    };

    recordData(commonDt);
    goto done;
  } else if (currMode == REPLAY) {
    MPI_UIRecvWait commonDt = {0};
    commonDt.callType = MPIIRECV;
    replayData(commonDt);
    MPI_IRecvCallSign *mpiSign = (MPI_IRecvCallSign*)(commonDt.callbuffer);
    buf = mpiSign -> buffer;
    *request = mpiSign -> request;
    result = mpiSign -> result;
    goto done;
  }

  result = NEXT_FNC(MPI_Irecv)(buf, count, datatype, source, tag, comm, request);
done:
  return result;
}

static void
checkpoint()
{
  if (logFile) {
    fclose(logFile);
  }
}

static void
restart()
{
  char mpiRecord[10] = {0};
  char mpiReplay[10] = {0};
  char pidbuf[100] = {0};

  dmtcp::Util::allowGdbDebug(MPI_WRAPPER_GDB);
  snprintf(pidbuf, sizeof(pidbuf), "%d", getpid());

  dmtcp::string filename = dmtcp_get_ckpt_dir();

  filename += "/dump.";
  filename += pidbuf;
  filename += ".log";
  JNOTE("record-replay file:")(filename);

  int ret = dmtcp_get_restart_env(ENV_MPI_RECORD, mpiRecord,
                                  sizeof(mpiRecord) - 1);
  JTRACE("MPI Recording") (mpiRecord);
  errCheckGetRestartEnv(ret);

  if (ret == SUCCESS) {
    currMode = RECORD;
    goto done;
  }

  ret = dmtcp_get_restart_env(ENV_MPI_REPLAY, mpiReplay,
                              sizeof(mpiReplay) - 1);
  JTRACE("MPI Replaying") (mpiReplay);
  errCheckGetRestartEnv(ret);

  if (ret == SUCCESS) {
    currMode = REPLAY;
    goto done;
  }

done:
  if (currMode == RECORD) {
    logFile = fopen(filename.c_str(), "w");
  } else if (currMode == REPLAY) {
    logFile = fopen(filename.c_str(), "r");
  } else {
    logFile = NULL;
  }
}

static DmtcpBarrier barriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESTART, restart, "restart" }
};

DmtcpPluginDescriptor_t mpiwrapper_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "mpiwrapper",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "MPI wrapper plugin",
  DMTCP_DECL_BARRIERS(barriers),
  NULL
};

DMTCP_DECL_PLUGIN(mpiwrapper_plugin);
