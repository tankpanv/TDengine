/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "vnd.h"
// use traft impl
#include "raft.h"

struct SVSync {
  struct raft_fsm fsm;
};

SRaftEnv raftEnv;

static void* raftEnvLoop(void* param);
static void  vndSyncApplyCb(struct raft_apply* req, int status, void* result);
static int vndFSMApplyCb(struct raft_fsm *fsm, const struct raft_buffer *buf, void **result, raft_index index);

// Sub-system level apis
int vndInitSync(const char* host, uint16_t port, const char* baseDir) {
  pthread_t tid;

  if (raftEnvInit(&raftEnv, host, port, baseDir) != 0) {
    // TODO
    return -1;
  }

  pthread_create(&tid, NULL, raftEnvLoop, &raftEnv);

  return 0;
}

int vndClearSync() {
  raftEnvStart(&raftEnv);
  return 0;
}

static void* raftEnvLoop(void* param) {
  SRaftEnv* pRaftEnv = (SRaftEnv*)param;
  raftEnvStart(pRaftEnv);
  return NULL;
}

int vndOpenSync(SVnode* pVnode) {
  pVnode->pSync = calloc(1, sizeof(SVSync));
  if (pVnode->pSync == NULL) {
    return -1;
  }

  pVnode->pSync->fsm.version = 0;
  pVnode->pSync->fsm.data = pVnode;
  pVnode->pSync->fsm.apply = vndFSMApplyCb;     // todo
  pVnode->pSync->fsm.snapshot = NULL;  // todo
  pVnode->pSync->fsm.restore = NULL;   // todo

  addRaftVoter(&raftEnv, NULL, 0, pVnode->vgId, &(pVnode->pSync->fsm));

  return 0;
}

int vndCloseSync(SVnode* pVnode) {
  if (pVnode->pSync) {
    free(pVnode->pSync);
  }
  return 0;
}

int vndSyncMsgs(SVnode* pVnode, SArray* pMsgs) {
  struct raft_apply*  req;
  int                 nMsg;
  struct raft_buffer* buffers;

  req = calloc(1, sizeof(*req));
  req->data = pVnode;

  nMsg = taosArrayGetSize(pMsgs);
  buffers = calloc(nMsg, sizeof(*buffers));

  for (int i = 0; i < nMsg; i++) {
    SRpcMsg* pMsg = (SRpcMsg*)taosArrayGetP(pMsgs, i);

    buffers[i].base = pMsg->pCont;
    buffers[i].len = pMsg->contLen;
  }

  raft_apply(getRaft(&raftEnv, pVnode->vgId), req, buffers, nMsg, vndSyncApplyCb);

  return 0;
}

static void vndSyncApplyCb(struct raft_apply* req, int status, void* result) {
  if (status == 0) {
    // return success
  } else {
    // return failure
  }
}

static int vndFSMApplyCb(struct raft_fsm *fsm, const struct raft_buffer *buf, void **result, raft_index index) {
  SVnode *pVnode = (SVnode *)fsm->data;
  // TODO
  return 0;
}