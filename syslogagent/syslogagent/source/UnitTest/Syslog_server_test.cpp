/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

#include "stdafx.h"
#if 0
#include "CppUnitTest.h"
#include "Syslog_server.h"
#include "Test_logger.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Syslog_agent;

class Test_connection: public Network_connection {
public:
    Result open(const wchar_t* host, const wchar_t* port, int protocol) override;
    Result ping(const wchar_t* host) override;
    Result send(const char* data, int length) override;
    void close() override;
    time_t now() override;
    void wait() override;
    string last_sent;
    int wait_count;
    int open_count;
    int ping_count;
    int close_count;
    time_t clock;
};

Result Test_connection::open(const wchar_t* host, const wchar_t* port, int protocol) {
    open_count++;
    return wcscmp(host, L"goodhost") == 0 ? Result() : Result(123, "bad open");
}

Result Test_connection::ping(const wchar_t* host) {
    ping_count++;
    return wcscmp(host, L"goodhost") == 0 ? Result() : Result(123, "bad ping");
}

Result Test_connection::send(const char* data, int length) {
    last_sent.assign(data, length);
    return strstr(data, "bad") == nullptr ? Result() : Result(123, "bad send");;
}

void Test_connection::wait() {
    wait_count++;
}

void Test_connection::close() {
    close_count++;
}

time_t Test_connection::now() {
    return clock;
}


TEST_CLASS(Syslog_server_test) {
public:
    string data;
    Test_connection* connection;

    TEST_METHOD(data_sent_if_open_good) {
        Syslog_server server(connection, Syslog_server::primary);
        server.open(L"goodhost", L"123");
        server.send(data);
        Assert::AreEqual(data.c_str(), connection->last_sent.c_str());
        Assert::IsTrue(server.has_sent());
    }

    TEST_METHOD(send_skipped_if_open_bad) {
        Syslog_server server(connection, Syslog_server::primary);
        server.open(L"badhost", L"123");
        server.send(data);
        Assert::AreEqual(0, (int)connection->last_sent.size());
        Assert::IsFalse(server.has_sent());
    }

    TEST_METHOD(waits_2_after_open_bad) {
        Syslog_server server(connection, Syslog_server::primary);
        for (auto i = 0; i < 4; i++) server.open(L"badhost", L"123");
        Assert::AreEqual(2, connection->wait_count);
        Assert::AreEqual(2, connection->open_count);
    }

    TEST_METHOD(waits_10_after_4_open_tries_bad) {
        Syslog_server server(connection, Syslog_server::primary);
        for (auto i = 0; i < 21; i++) server.open(L"badhost", L"123");
        Assert::AreEqual(16, connection->wait_count);
        Assert::AreEqual(5, connection->open_count);
    }

    TEST_METHOD(waits_60_after_11_open_tries_bad) {
        Syslog_server server(connection, Syslog_server::primary);
        for (auto i = 0; i < 148; i++) server.open(L"badhost", L"123");
        Assert::AreEqual(136, connection->wait_count);
        Assert::AreEqual(12, connection->open_count);
    }

    TEST_METHOD(no_waits_if_not_retrying) {
        Syslog_server server(connection, Syslog_server::secondary);
        for (auto i = 0; i < 4; i++) server.open(L"badhost", L"123");
        Assert::AreEqual(0, connection->wait_count);
        Assert::AreEqual(1, connection->open_count);
    }

    TEST_METHOD(opens_after_idle) {
        Syslog_server server(connection, Syslog_server::secondary);
        for (auto i = 0; i < 3; i++) server.open(L"badhost", L"123");
        server.idle();
        server.open(L"badhost", L"123");
        Assert::AreEqual(2, connection->open_count);
    }

    TEST_METHOD(closes_if_send_fails) {
        Syslog_server server(connection, Syslog_server::primary);
        server.open(L"goodhost", L"123");
        data = "baddata";
        server.send(data);
        Assert::AreEqual(1, connection->close_count);
    }

    TEST_METHOD(primary_server_does_not_ping) {
        Syslog_server server(connection, Syslog_server::primary);
        connection->clock = 21;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(0, connection->ping_count);
    }

    TEST_METHOD(secondary_server_does_not_ping) {
        Syslog_server server(connection, Syslog_server::secondary);
        connection->clock = 21;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(0, connection->ping_count);
    }

    TEST_METHOD(configured_server_pings_every_20_seconds) {
        Syslog_server server(connection, Syslog_server::primary | Syslog_server::pings);
        connection->clock = 21;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(1, connection->ping_count);
        connection->clock = 40;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(1, connection->ping_count);
        connection->clock = 41;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(2, connection->ping_count);
    }

    TEST_METHOD(configured_server_pings_before_opening) {
        Syslog_server server(connection, Syslog_server::primary | Syslog_server::pings);
        connection->clock = 21;
        server.open(L"goodhost", L"123");
        Assert::AreEqual(1, connection->ping_count);
        server.close();
        server.open(L"goodhost", L"123");
        Assert::AreEqual(2, connection->ping_count);
    }

    TEST_METHOD(waits_2_after_ping_bad) {
        Syslog_server server(connection, Syslog_server::primary | Syslog_server::pings);
        connection->clock = 21;
        for (auto i = 0; i < 4; i++) server.open(L"badhost", L"123");
        Assert::AreEqual(2, connection->wait_count);
        Assert::AreEqual(0, connection->open_count);
        Assert::AreEqual(2, connection->ping_count);
    }

    TEST_METHOD(sends_prefix_with_count_for_tcp) {
        Syslog_server server(connection, Syslog_server::primary | Syslog_server::tcp);
        server.open(L"goodhost", L"123");
        server.send(data);
        Assert::AreEqual("5 stuff", connection->last_sent.c_str());
    }

    TEST_METHOD(closes_tcp_connection_after_idle_interval) {
        connection->clock = 20;
        Syslog_server server(connection, Syslog_server::primary | Syslog_server::tcp);
        server.open(L"goodhost", L"123");
        server.send(data);
        Assert::AreEqual(0, connection->close_count);
        server.idle();
        Assert::AreEqual(0, connection->close_count);
        connection->clock = 79;
        server.idle();
        Assert::AreEqual(0, connection->close_count);
        connection->clock = 80;
        server.idle();
        Assert::AreEqual(1, connection->close_count);
    }

    TEST_METHOD(does_not_close_udp_connection_after_idle_interval) {
        connection->clock = 20;
        Syslog_server server(connection, Syslog_server::primary);
        server.open(L"goodhost", L"123");
        server.send(data);
        connection->clock = 80;
        server.idle();
        Assert::AreEqual(0, connection->close_count);
    }

    TEST_METHOD_INITIALIZE(setup) {
        set_test_logger();
        data = "stuff";
        connection = new Test_connection();
    }

    TEST_METHOD_CLEANUP(tear_down) {
        delete connection;
    }
};

#endif