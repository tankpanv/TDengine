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

#define _DEFAULT_SOURCE
#include "sdbInt.h"

const char *sdbTableName(ESdbType type) {
  switch (type) {
    case SDB_TRANS:
      return "trans";
    case SDB_CLUSTER:
      return "cluster";
    case SDB_MNODE:
      return "mnode";
    case SDB_QNODE:
      return "qnode";
    case SDB_SNODE:
      return "snode";
    case SDB_BNODE:
      return "bnode";
    case SDB_DNODE:
      return "dnode";
    case SDB_USER:
      return "user";
    case SDB_AUTH:
      return "auth";
    case SDB_ACCT:
      return "acct";
    case SDB_CONSUMER:
      return "consumer";
    case SDB_CGROUP:
      return "cgroup";
    case SDB_TOPIC:
      return "topic";
    case SDB_VGROUP:
      return "vgId";
    case SDB_STB:
      return "stb";
    case SDB_DB:
      return "db";
    case SDB_FUNC:
      return "func";
    default:
      return "undefine";
  }
}

static const char *sdbStatusStr(ESdbStatus status) {
  switch (status) {
    case SDB_STATUS_CREATING:
      return "creating";
    case SDB_STATUS_UPDATING:
      return "updating";
    case SDB_STATUS_DROPPING:
      return "dropping";
    case SDB_STATUS_READY:
      return "ready";
    case SDB_STATUS_DROPPED:
      return "dropped";
    case SDB_STATUS_INIT:
      return "init";
    default:
      return "undefine";
  }
}

void sdbPrintOper(SSdb *pSdb, SSdbRow *pRow, const char *oper) {
  EKeyType keyType = pSdb->keyTypes[pRow->type];

  if (keyType == SDB_KEY_BINARY) {
    mTrace("%s:%s, refCount:%d oper:%s row:%p status:%s", sdbTableName(pRow->type), (char *)pRow->pObj, pRow->refCount,
           oper, pRow->pObj, sdbStatusStr(pRow->status));
  } else if (keyType == SDB_KEY_INT32) {
    mTrace("%s:%d, refCount:%d oper:%s row:%p status:%s", sdbTableName(pRow->type), *(int32_t *)pRow->pObj,
           pRow->refCount, oper, pRow->pObj, sdbStatusStr(pRow->status));
  } else if (keyType == SDB_KEY_INT64) {
    mTrace("%s:%" PRId64 ", refCount:%d oper:%s row:%p status:%s", sdbTableName(pRow->type), *(int64_t *)pRow->pObj,
           pRow->refCount, oper, pRow->pObj, sdbStatusStr(pRow->status));
  } else {
  }
}

static SHashObj *sdbGetHash(SSdb *pSdb, int32_t type) {
  if (type >= SDB_MAX || type < 0) {
    terrno = TSDB_CODE_SDB_INVALID_TABLE_TYPE;
    return NULL;
  }

  SHashObj *hash = pSdb->hashObjs[type];
  if (hash == NULL) {
    terrno = TSDB_CODE_SDB_APP_ERROR;
    return NULL;
  }

  return hash;
}

static int32_t sdbGetkeySize(SSdb *pSdb, ESdbType type, void *pKey) {
  int32_t  keySize;
  EKeyType keyType = pSdb->keyTypes[type];

  if (keyType == SDB_KEY_INT32) {
    keySize = sizeof(int32_t);
  } else if (keyType == SDB_KEY_BINARY) {
    keySize = strlen(pKey) + 1;
  } else {
    keySize = sizeof(int64_t);
  }

  return keySize;
}

static int32_t sdbInsertRow(SSdb *pSdb, SHashObj *hash, SSdbRaw *pRaw, SSdbRow *pRow, int32_t keySize) {
  SRWLatch *pLock = &pSdb->locks[pRow->type];
  taosWLockLatch(pLock);

  SSdbRow *pOldRow = taosHashGet(hash, pRow->pObj, keySize);
  if (pOldRow != NULL) {
    taosWUnLockLatch(pLock);
    sdbFreeRow(pSdb, pRow);
    terrno = TSDB_CODE_SDB_OBJ_ALREADY_THERE;
    return terrno;
  }

  pRow->refCount = 0;
  pRow->status = pRaw->status;
  sdbPrintOper(pSdb, pRow, "insertRow");

  if (taosHashPut(hash, pRow->pObj, keySize, &pRow, sizeof(void *)) != 0) {
    taosWUnLockLatch(pLock);
    sdbFreeRow(pSdb, pRow);
    terrno = TSDB_CODE_SDB_OBJ_ALREADY_THERE;
    return terrno;
  }

  taosWUnLockLatch(pLock);

  int32_t     code = 0;
  SdbInsertFp insertFp = pSdb->insertFps[pRow->type];
  if (insertFp != NULL) {
    code = (*insertFp)(pSdb, pRow->pObj);
    if (code != 0) {
      taosWLockLatch(pLock);
      taosHashRemove(hash, pRow->pObj, keySize);
      taosWUnLockLatch(pLock);
      sdbFreeRow(pSdb, pRow);
      terrno = code;
      return terrno;
    }
  }

  if (pSdb->keyTypes[pRow->type] == SDB_KEY_INT32) {
    pSdb->maxId[pRow->type] = MAX(pSdb->maxId[pRow->type], *((int32_t *)pRow->pObj));
  }
  if (pSdb->keyTypes[pRow->type] == SDB_KEY_INT64) {
    pSdb->maxId[pRow->type] = MAX(pSdb->maxId[pRow->type], *((int32_t *)pRow->pObj));
  }
  pSdb->tableVer[pRow->type]++;

  return 0;
}

static int32_t sdbUpdateRow(SSdb *pSdb, SHashObj *hash, SSdbRaw *pRaw, SSdbRow *pNewRow, int32_t keySize) {
  SRWLatch *pLock = &pSdb->locks[pNewRow->type];
  taosRLockLatch(pLock);

  SSdbRow **ppOldRow = taosHashGet(hash, pNewRow->pObj, keySize);
  if (ppOldRow == NULL || *ppOldRow == NULL) {
    taosRUnLockLatch(pLock);
    return sdbInsertRow(pSdb, hash, pRaw, pNewRow, keySize);
  }

  SSdbRow *pOldRow = *ppOldRow;
  pOldRow->status = pRaw->status;
  sdbPrintOper(pSdb, pOldRow, "updateRow");
  taosRUnLockLatch(pLock);

  int32_t     code = 0;
  SdbUpdateFp updateFp = pSdb->updateFps[pNewRow->type];
  if (updateFp != NULL) {
    code = (*updateFp)(pSdb, pOldRow->pObj, pNewRow->pObj);
  }

  sdbFreeRow(pSdb, pNewRow);

  pSdb->tableVer[pOldRow->type]++;
  return code;
}

static int32_t sdbDeleteRow(SSdb *pSdb, SHashObj *hash, SSdbRaw *pRaw, SSdbRow *pRow, int32_t keySize) {
  SRWLatch *pLock = &pSdb->locks[pRow->type];
  taosWLockLatch(pLock);

  SSdbRow **ppOldRow = taosHashGet(hash, pRow->pObj, keySize);
  if (ppOldRow == NULL || *ppOldRow == NULL) {
    taosWUnLockLatch(pLock);
    sdbFreeRow(pSdb, pRow);
    terrno = TSDB_CODE_SDB_OBJ_NOT_THERE;
    return terrno;
  }
  SSdbRow *pOldRow = *ppOldRow;

  pOldRow->status = pRaw->status;
  sdbPrintOper(pSdb, pOldRow, "deleteRow");

  taosHashRemove(hash, pOldRow->pObj, keySize);
  taosWUnLockLatch(pLock);

  pSdb->tableVer[pOldRow->type]++;
  sdbFreeRow(pSdb, pRow);
  // sdbRelease(pSdb, pOldRow->pObj);
  return 0;
}

int32_t sdbWriteNotFree(SSdb *pSdb, SSdbRaw *pRaw) {
  SHashObj *hash = sdbGetHash(pSdb, pRaw->type);
  if (hash == NULL) return terrno;

  SdbDecodeFp decodeFp = pSdb->decodeFps[pRaw->type];
  SSdbRow    *pRow = (*decodeFp)(pRaw);
  if (pRow == NULL) {
    return terrno;
  }

  pRow->type = pRaw->type;

  int32_t keySize = sdbGetkeySize(pSdb, pRow->type, pRow->pObj);
  int32_t code = TSDB_CODE_SDB_INVALID_ACTION_TYPE;

  switch (pRaw->status) {
    case SDB_STATUS_CREATING:
      code = sdbInsertRow(pSdb, hash, pRaw, pRow, keySize);
      break;
    case SDB_STATUS_UPDATING:
    case SDB_STATUS_READY:
    case SDB_STATUS_DROPPING:
      code = sdbUpdateRow(pSdb, hash, pRaw, pRow, keySize);
      break;
    case SDB_STATUS_DROPPED:
      code = sdbDeleteRow(pSdb, hash, pRaw, pRow, keySize);
      break;
  }

  return code;
}

int32_t sdbWrite(SSdb *pSdb, SSdbRaw *pRaw) {
  int32_t code = sdbWriteNotFree(pSdb, pRaw);
  sdbFreeRaw(pRaw);
  return code;
}

void *sdbAcquire(SSdb *pSdb, ESdbType type, void *pKey) {
  terrno = 0;

  SHashObj *hash = sdbGetHash(pSdb, type);
  if (hash == NULL) return NULL;

  void   *pRet = NULL;
  int32_t keySize = sdbGetkeySize(pSdb, type, pKey);

  SRWLatch *pLock = &pSdb->locks[type];
  taosRLockLatch(pLock);

  SSdbRow **ppRow = taosHashGet(hash, pKey, keySize);
  if (ppRow == NULL || *ppRow == NULL) {
    taosRUnLockLatch(pLock);
    terrno = TSDB_CODE_SDB_OBJ_NOT_THERE;
    return NULL;
  }

  SSdbRow *pRow = *ppRow;
  switch (pRow->status) {
    case SDB_STATUS_READY:
    case SDB_STATUS_UPDATING:
      atomic_add_fetch_32(&pRow->refCount, 1);
      pRet = pRow->pObj;
      sdbPrintOper(pSdb, pRow, "acquireRow");
      break;
    case SDB_STATUS_CREATING:
      terrno = TSDB_CODE_SDB_OBJ_CREATING;
      break;
    case SDB_STATUS_DROPPING:
      terrno = TSDB_CODE_SDB_OBJ_DROPPING;
      break;
    default:
      terrno = TSDB_CODE_SDB_APP_ERROR;
      break;
  }

  taosRUnLockLatch(pLock);
  return pRet;
}

void sdbRelease(SSdb *pSdb, void *pObj) {
  if (pObj == NULL) return;

  SSdbRow *pRow = (SSdbRow *)((char *)pObj - sizeof(SSdbRow));
  if (pRow->type >= SDB_MAX ) return;

  SRWLatch *pLock = &pSdb->locks[pRow->type];
  taosRLockLatch(pLock);

  int32_t ref = atomic_sub_fetch_32(&pRow->refCount, 1);
  sdbPrintOper(pSdb, pRow, "releaseRow");
  if (ref <= 0 && pRow->status == SDB_STATUS_DROPPED) {
    sdbFreeRow(pSdb, pRow);
  }

  taosRUnLockLatch(pLock);
}

void *sdbFetch(SSdb *pSdb, ESdbType type, void *pIter, void **ppObj) {
  *ppObj = NULL;

  SHashObj *hash = sdbGetHash(pSdb, type);
  if (hash == NULL) return NULL;

  SRWLatch *pLock = &pSdb->locks[type];
  taosRLockLatch(pLock);

  if (pIter != NULL) {
    SSdbRow *pLastRow = *(SSdbRow **)pIter;
    int32_t  ref = atomic_load_32(&pLastRow->refCount);
    if (ref <= 0 && pLastRow->status == SDB_STATUS_DROPPED) {
      sdbFreeRow(pSdb, pLastRow);
    }
  }

  SSdbRow **ppRow = taosHashIterate(hash, pIter);
  while (ppRow != NULL) {
    SSdbRow *pRow = *ppRow;
    if (pRow == NULL || pRow->status != SDB_STATUS_READY) {
      ppRow = taosHashIterate(hash, ppRow);
      continue;
    }

    atomic_add_fetch_32(&pRow->refCount, 1);
    sdbPrintOper(pSdb, pRow, "fetchRow");
    *ppObj = pRow->pObj;
    break;
  }
  taosRUnLockLatch(pLock);

  return ppRow;
}

void sdbCancelFetch(SSdb *pSdb, void *pIter) {
  if (pIter == NULL) return;
  SSdbRow  *pRow = *(SSdbRow **)pIter;
  SHashObj *hash = sdbGetHash(pSdb, pRow->type);
  if (hash == NULL) return;

  SRWLatch *pLock = &pSdb->locks[pRow->type];
  taosRLockLatch(pLock);
  taosHashCancelIterate(hash, pIter);
  taosRUnLockLatch(pLock);
}

void sdbTraverse(SSdb *pSdb, ESdbType type, sdbTraverseFp fp, void *p1, void *p2, void *p3) {
  SHashObj *hash = sdbGetHash(pSdb, type);
  if (hash == NULL) return;

  SRWLatch *pLock = &pSdb->locks[type];
  taosRLockLatch(pLock);

  SSdbRow **ppRow = taosHashIterate(hash, NULL);
  while (ppRow != NULL) {
    SSdbRow *pRow = *ppRow;
    if (pRow->status == SDB_STATUS_READY) {
      bool isContinue = (*fp)(pSdb->pMnode, pRow->pObj, p1, p2, p3);
      if (!isContinue) {
        taosHashCancelIterate(hash, ppRow);
        break;
      }
    }

    ppRow = taosHashIterate(hash, ppRow);
  }

  taosRUnLockLatch(pLock);
}

int32_t sdbGetSize(SSdb *pSdb, ESdbType type) {
  SHashObj *hash = sdbGetHash(pSdb, type);
  if (hash == NULL) return 0;

  SRWLatch *pLock = &pSdb->locks[type];
  taosRLockLatch(pLock);
  int32_t size = taosHashGetSize(hash);
  taosRUnLockLatch(pLock);

  return size;
}

int32_t sdbGetMaxId(SSdb *pSdb, ESdbType type) {
  SHashObj *hash = sdbGetHash(pSdb, type);
  if (hash == NULL) return -1;

  if (pSdb->keyTypes[type] != SDB_KEY_INT32) return -1;

  int32_t maxId = 0;

  SRWLatch *pLock = &pSdb->locks[type];
  taosRLockLatch(pLock);

  SSdbRow **ppRow = taosHashIterate(hash, NULL);
  while (ppRow != NULL) {
    SSdbRow *pRow = *ppRow;
    int32_t  id = *(int32_t *)pRow->pObj;
    maxId = MAX(id, maxId);
    ppRow = taosHashIterate(hash, ppRow);
  }

  taosRUnLockLatch(pLock);

  maxId = MAX(maxId, pSdb->maxId[type]);
  return maxId + 1;
}
