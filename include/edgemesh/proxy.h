#ifndef EDGEMESH_PROXY_H
#define EDGEMESH_PROXY_H

#include "edgemesh/backend_pool.h"

int em_proxy_run(unsigned short listen_port, unsigned short backend_base_port, em_policy policy);

#endif
