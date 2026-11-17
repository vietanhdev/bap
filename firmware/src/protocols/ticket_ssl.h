// TLS transport with session-ticket resumption, used for the Gemini Live WebSocket.
//
// Derived from EspSsl in 78/esp-ml307 (Apache License 2.0,
// https://github.com/78/esp-ml307). Changes: caches the TLS session per host and offers it on the
// next connection, so a reconnect skips the expensive ECDHE/certificate work (~1 s on the S3).
#pragma once

#include <esp_tls.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <memory>
#include <mutex>
#include <string>

#include "http_client.h"
#include "network_interface.h"
#include "tcp.h"

class TicketSsl : public Tcp {
public:
    TicketSsl();
    ~TicketSsl() override;

    NetworkResult<> Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;

    // Drops every cached session (e.g. after the server rejected one).
    static void ForgetSessions();
    // Code and reason of the last WebSocket close frame a server sent ("1011 ..."), then clears it.
    static std::string TakeCloseReason();

private:
    esp_tls_t* tls_client_ = nullptr;
    int sockfd_ = -1;
    std::mutex tls_mutex_;
    EventGroupHandle_t event_group_ = nullptr;
    TaskHandle_t receive_task_handle_ = nullptr;

    void ReceiveTask();
    static void RememberCloseFrame(const std::string& chunk);
};

// Forwards everything to the board's network, except TLS connections, which use TicketSsl.
class TicketNetwork : public NetworkInterface {
public:
    explicit TicketNetwork(NetworkInterface* base) : base_(base) {}
    std::unique_ptr<Http> CreateHttp(int id = -1) override {
        return std::make_unique<HttpClient>(this, id);  // HTTPS through TicketSsl as well
    }
    std::unique_ptr<Tcp> CreateTcp(int id = -1) override { return base_->CreateTcp(id); }
    std::unique_ptr<Tcp> CreateSsl(int) override { return std::make_unique<TicketSsl>(); }
    std::unique_ptr<Udp> CreateUdp(int id = -1) override { return base_->CreateUdp(id); }
    std::unique_ptr<Mqtt> CreateMqtt(int id = -1) override { return base_->CreateMqtt(id); }
    std::unique_ptr<WebSocket> CreateWebSocket(int id = -1) override {
        return std::make_unique<WebSocket>(this, id);
    }

private:
    NetworkInterface* base_;
};

// The board network with TLS session resumption; use it for every outgoing connection.
NetworkInterface* FastNetwork();
