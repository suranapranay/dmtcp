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
#define MPIRECV 	    3

static inline size_t
getMpiDtSize(MPI_Datatype dt)
{
  uint8_t ret = 1;
  switch (dt) {
    case MPI_LONG:
    case MPI_UNSIGNED_LONG:
    case MPI_LONG_LONG:
    case MPI_DOUBLE: ret = 8; break;
    case MPI_UNSIGNED:
    case MPI_FLOAT:
    case MPI_INT: ret = 4; break;
    case MPI_UNSIGNED_CHAR:
    case MPI_CHAR:
    case MPI_BYTE: ret = 1; break;
    default: break;
  }
  return ret;
}

struct _MPISignature{
 int count;
 MPI_Datatype mDtype;
 int src;
 int tag;
 MPI_Comm comm;
 MPI_Status status;
 int result;
 char *buffer;
};

typedef struct _MPISignature MPI_RecvCallSign;


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
  JNOTE("Start Recording");
  JASSERT(logFile);
  JASSERT(currMode == RECORD);
  int callType = mpiRecvWait.callType;
  int sz;
  int datasz;
  if(callType == MPIIRECV){
      JNOTE("beginning irecv record");
      sz = sizeof(MPI_IRecvCallSign);
      MPI_IRecvCallSign* mpiIRecvData = (MPI_IRecvCallSign*) mpiRecvWait.callbuffer;
      JNOTE("pointer set");
      datasz = mpiIRecvData -> count * getMpiDtSize(mpiIRecvData -> mDtype);
      JNOTE("setup data");
      fwrite(&mpiRecvWait, sizeof(MPI_UIRecvWait), 1, logFile);
      fwrite(mpiRecvWait.callbuffer, sz, 1, logFile);
      fwrite(mpiIRecvData -> buffer, datasz, 1, logFile);
      JNOTE("DONE RECORDING RECV");
  }
  else if (callType == MPIWAIT){
      fwrite(&mpiRecvWait, sizeof(MPI_UIRecvWait), 1, logFile);
      fwrite(mpiRecvWait.callbuffer, sizeof(MPI_WaitSign), 1, logFile);
      JNOTE("RECORDING wait");
}
  else if(callType == MPIRECV){
      sz = sizeof(MPI_RecvCallSign);
      MPI_RecvCallSign* mpiRecvData = (MPI_RecvCallSign*) mpiRecvWait.callbuffer;
      datasz = mpiRecvData -> count * getMpiDtSize(mpiRecvData -> mDtype);
      fwrite(&mpiRecvWait, sizeof(MPI_UIRecvWait), 1, logFile);
      fwrite(mpiRecvWait.callbuffer, sz, 1, logFile);
      fwrite(mpiRecvData -> buffer, datasz, 1, logFile);
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

  else if(_replayData.callType == MPIRECV){
    MPI_RecvCallSign *mpiRecv = new MPI_RecvCallSign();
    fread(mpiRecv, sizeof(MPI_RecvCallSign), 1, logFile);
    mpiRecv -> buffer = (char*)malloc(getMpiDtSize(mpiRecv -> mDtype) * mpiRecv -> count);
    fread(mpiRecv -> buffer, getMpiDtSize(mpiRecv -> mDtype) * mpiRecv -> count, 1, logFile);
    _replayData.callbuffer = mpiRecv;
}

  else{
    MPI_IRecvCallSign *irecv = new MPI_IRecvCallSign();
    fread(irecv, sizeof(MPI_IRecvCallSign), 1, logFile);
    irecv -> buffer = (char*)malloc(getMpiDtSize(irecv -> mDtype) * irecv -> count);
    fread(irecv -> buffer, getMpiDtSize(irecv -> mDtype) * irecv -> count, 1, logFile);
    _replayData.callbuffer = irecv;
  }
  return;

}

int MPI_Recv(void *buf, int count, MPI_Datatype dt,
             int src, int tag, MPI_Comm comm, MPI_Status *stat)
{
  int result = 0;
  if (currMode == RECORD) {
    result = NEXT_FNC(MPI_Recv)(buf, count, dt, src, tag, comm, stat);
    MPI_RecvCallSign mpiSign = { .count = count,
                             .mDtype = dt,
                             .src = src,
                             .tag = tag,
                             .comm = comm,
                             .status = *stat,
                             .result = result,
                             .buffer = (char*)buf};

    MPI_UIRecvWait commonDt = {
	    .callType = MPIRECV,
	    .callbuffer = &mpiSign
    };



    recordData(commonDt);
    goto done;
  } else if (currMode == REPLAY) {
    MPI_UIRecvWait commonDt = {0};
    commonDt.callType = MPIRECV;
    replayData(commonDt);
    MPI_RecvCallSign *mpiSign = (MPI_RecvCallSign*)commonDt.callbuffer;
    memcpy(buf, mpiSign -> buffer, mpiSign -> count * getMpiDtSize(mpiSign -> mDtype)) ;
    result = mpiSign -> result;
    free(mpiSign -> buffer);
    goto done;
  }

  result = NEXT_FNC(MPI_Recv)(buf, count, dt, src, tag, comm, stat);
done:
  return result;
}




int MPI_Wait(MPI_Request *request, MPI_Status *status)
{
  int result = 0;
  MPI_WaitSign mpiWait;
  mpiWait.request = *request;
  mpiWait.status = *status;
  if (currMode == RECORD) {
    result = NEXT_FNC(MPI_Wait)(request, status);
    MPI_UIRecvWait commonDt = {
        .callType = MPIWAIT,
        .callbuffer = &mpiWait
    };

    recordData(commonDt);
    goto done;
  }
  else if (currMode == REPLAY) {
    MPI_UIRecvWait commonDt = {0};
    commonDt.callType = MPIWAIT;
    replayData(commonDt);
    MPI_WaitSign mpiSign = *(MPI_WaitSign*)(commonDt.callbuffer);
    *request = (mpiSign.request);
    *status = (mpiSign.status);
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
  JNOTE("inside Recv");
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
        .callbuffer = &mpiSign
    };
  JNOTE("begin recording WAIT");
    recordData(commonDt);
  JNOTE("done recording WAIT");
    goto done;
  } else if (currMode == REPLAY) {
    MPI_UIRecvWait commonDt = {0};
    commonDt.callType = MPIIRECV;
    replayData(commonDt);
    MPI_IRecvCallSign *mpiSign = (MPI_IRecvCallSign*)(commonDt.callbuffer);
    memcpy(buf, mpiSign -> buffer, mpiSign -> count * getMpiDtSize(datatype)) ;
    free(mpiSign -> buffer);
    memcpy(request, &(mpiSign -> request), sizeof(MPI_Request));
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
