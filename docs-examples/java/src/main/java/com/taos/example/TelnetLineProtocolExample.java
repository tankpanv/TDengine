package com.taos.example;

import com.taosdata.jdbc.SchemalessWriter;
import com.taosdata.jdbc.enums.SchemalessProtocolType;
import com.taosdata.jdbc.enums.SchemalessTimestampType;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;

public class TelnetLineProtocolExample {
    // format: <metric> <timestamp> <value> <tagk_1>=<tagv_1>[ <tagk_n>=<tagv_n>]
    private static String[] lines = {"meters.current 1648432611249 10.3 location=Beijing.Chaoyang groupid=2",
            "meters.current 1648432611250 12.6 location=Beijing.Chaoyang groupid=2",
            "meters.current 1648432611249 10.8 location=Beijing.Haidian groupid=3",
            "meters.current 1648432611250 11.3 location=Beijing.Haidian groupid=3",
            "meters.voltage 1648432611249 219 location=Beijing.Chaoyang groupid=2",
            "meters.voltage 1648432611250 218 location=Beijing.Chaoyang groupid=2",
            "meters.voltage 1648432611249 221 location=Beijing.Haidian groupid=3",
            "meters.voltage 1648432611250 217 location=Beijing.Haidian groupid=3",
    };

    private static Connection getConnection() throws SQLException {
        String jdbcUrl = "jdbc:TAOS://localhost:6030?user=root&password=taosdata";
        return DriverManager.getConnection(jdbcUrl);
    }

    private static void createDatabase(Connection conn) throws SQLException {
        try (Statement stmt = conn.createStatement()) {
            // the default precision is ms (microsecond), but we use us(microsecond) here.
            stmt.execute("create database test precision 'us'");
            stmt.execute("use test");
        }
    }

    public static void main(String[] args) throws SQLException {
        try (Connection conn = getConnection()) {
            createDatabase(conn);
            SchemalessWriter writer = new SchemalessWriter(conn);
            writer.write(lines, SchemalessProtocolType.TELNET, SchemalessTimestampType.NOT_CONFIGURED);
        }
    }

}
