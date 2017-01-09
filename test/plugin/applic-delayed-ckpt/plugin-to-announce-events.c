#include <stdio.h>
#include "dmtcp.h"

static void
applic_delayed_ckpt_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    printf("Plugin(%s:%d): initialization of plugin is complete.\n",
           __FILE__, __LINE__);
    break;
  case DMTCP_EVENT_EXIT:
    printf("Plugin(%s:%d): exiting.\n", __FILE__, __LINE__);
    break;

  default:
    break;
  }
}

static void
checkpoint()
{
  printf("Plugin(%s:%d): about to checkpoint.\n", __FILE__, __LINE__);
}

static void
resume()
{
  printf("Plugin(%s:%d): done checkpointing.\n", __FILE__, __LINE__);
}

static void
restart()
{
  printf("Plugin(%s:%d): done restarting from checkpoint image.\n",
         __FILE__, __LINE__);
}

static DmtcpBarrier barriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESUME, resume, "resume" },
  { DMTCP_GLOBAL_BARRIER_RESTART, restart, "restart" }
};

DmtcpPluginDescriptor_t applic_delayed_ckpt_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "applic_delayed_ckpt",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Application delayed ckpt plugin",
  DMTCP_DECL_BARRIERS(barriers),
  applic_delayed_ckpt_event_hook
};

DMTCP_DECL_PLUGIN(applic_delayed_ckpt_plugin);
