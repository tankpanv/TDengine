##################################################################
#           Copyright (c) 2016 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-

import sys
import os
from util.log import *
from util.cases import *
from util.sql import *
from util.dnodes import *
import subprocess


class TDTestCase:
    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def getPath(self, tool="taosBenchmark"):
        selfPath = os.path.dirname(os.path.realpath(__file__))

        if ("community" in selfPath):
            projPath = selfPath[:selfPath.find("community")]
        else:
            projPath = selfPath[:selfPath.find("tests")]

        paths = []
        for root, dirs, files in os.walk(projPath):
            if ((tool) in files):
                rootRealPath = os.path.dirname(os.path.realpath(root))
                if ("packaging" not in rootRealPath):
                    paths.append(os.path.join(root, tool))
                    break
        return paths[0]

    def run(self):
        tdSql.prepare()
        binPath = self.getPath("taosBenchmark")
        if (binPath == ""):
            tdLog.exit("taosBenchmark not found!")
        else:
            tdLog.info("taosBenchmark found in %s" % binPath)
        taosdemoCmd = "%s -f tools/insert-interlace.json -G 2>&1 | grep sleep | wc -l" % binPath
        sleepTimes = subprocess.check_output(
            taosdemoCmd, shell=True).decode("utf-8")
        print("sleep times: %d" % int(sleepTimes))

        if (int(sleepTimes) != 16):
            caller = inspect.getframeinfo(inspect.stack()[0][0])
            tdLog.exit(
                "%s(%d) failed: expected sleep times 16, actual %d" %
                (caller.filename, caller.lineno, int(sleepTimes)))

        tdSql.execute("use db")
        tdSql.query("select count(tbname) from db.stb")
        tdSql.checkData(0, 0, 9)
        tdSql.query("select count(*) from db.stb")
        tdSql.checkData(0, 0, 2250)

    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
