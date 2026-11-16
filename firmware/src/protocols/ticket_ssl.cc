// Derived from esp_ssl.cc in 78/esp-ml307 (Apache License 2.0). Changes: class renamed to
// TicketSsl, TLS session tickets cached per host and offered on reconnect.
#include "ticket_ssl.h"

#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <map>

#include "board.h"

static const char* TAG = "TicketSsl";

#define ESP_SSL_EVENT_RECEIVE_TASK_EXIT 1
#define ESP_SSL_SEND_TIMEOUT_MS 10000

namespace {
std::mutex g_session_mutex;
std::map<std::string, esp_tls_client_session_t*> g_sessions;  // host -> last session
}  // namespace

NetworkInterface* FastNetwork() {
    static TicketNetwork network(Board::GetInstance().GetNetwork());
    return &network;
}

void TicketSsl::ForgetSessions() {
    std::lock_guard<std::mutex> lock(g_session_mutex);
    for (auto& [host, session] : g_sessions)
        esp_tls_free_client_session(session);
    g_sessions.clear();
}

// Wait until fd is readable and/or writable. A negative timeout waits forever.
static int WaitSocket(int fd, bool for_read, bool for_write, int timeout_ms) {
    fd_set read_fds;
    fd_set write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (for_read) {
        FD_SET(fd, &read_fds);
    }
    if (for_write) {
        FD_SET(fd, &write_fds);
    }
    struct timeval tv = {};
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    }
    return select(fd + 1, for_read ? &read_fds : nullptr, for_write ? &write_fds : nullptr, nullptr,
                  timeout_ms >= 0 ? &tv : nullptr);
}

TicketSsl::TicketSsl() { event_group_ = xEventGroupCreate(); }

TicketSsl::~TicketSsl() {
    Disconnect();

    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

NetworkResult<> TicketSsl::Connect(const std::string& host, int port) {
    // Also releases the TLS context and joins the receive task left by a
    // passive disconnect, so the object can be reconnected.
    Disconnect();

    tls_client_ = esp_tls_init();
    if (tls_client_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize TLS");
        return Fail(NetworkError::TlsFailed());
    }

    auto destroy_tls = [this]() {
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = nullptr;
        sockfd_ = -1;
    };

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    std::string key = host + ":" + std::to_string(port);
    bool resumed = false;
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        auto it = g_sessions.find(key);
        if (it != g_sessions.end()) {
            cfg.client_session = it->second;  // offer the ticket; the server may still refuse it
            resumed = true;
        }
    }

    int64_t t0 = esp_timer_get_time();
    int ret = esp_tls_conn_new_sync(host.c_str(), host.length(), port, &cfg, tls_client_);
    if (ret != 1 && resumed) {
        // A stale ticket must not cost the connection: forget it and do a full handshake.
        ESP_LOGW(TAG, "resumption failed, retrying with a full handshake");
        {
            std::lock_guard<std::mutex> lock(g_session_mutex);
            auto it = g_sessions.find(key);
            if (it != g_sessions.end()) {
                esp_tls_free_client_session(it->second);
                g_sessions.erase(it);
            }
        }
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = esp_tls_init();
        if (tls_client_ == nullptr)
            return Fail(NetworkError::TlsFailed());
        cfg.client_session = nullptr;
        resumed = false;
        ret = esp_tls_conn_new_sync(host.c_str(), host.length(), port, &cfg, tls_client_);
    }
    if (ret == 1) {
        ESP_LOGI(TAG, "TLS to %s in %d ms (%s)", host.c_str(),
                 (int)((esp_timer_get_time() - t0) / 1000),
                 resumed ? "ticket offered" : "full handshake");
        if (esp_tls_client_session_t* session = esp_tls_get_client_session(tls_client_)) {
            std::lock_guard<std::mutex> lock(g_session_mutex);
            auto& slot = g_sessions[key];
            if (slot != nullptr)
                esp_tls_free_client_session(slot);
            slot = session;
        }
    }
    if (ret != 1) {
        NetworkError err = NetworkError::TlsFailed();
        esp_tls_error_handle_t tls_error;
        if (esp_tls_get_error_handle(tls_client_, &tls_error) == ESP_OK) {
            int error_code, error_flags;
            esp_err_t esp_err =
                esp_tls_get_and_clear_last_error(tls_error, &error_code, &error_flags);
            err = NetworkError::FromEsp(esp_err);
            ESP_LOGE(TAG, "Failed to connect to %s:%d: %s", host.c_str(), port,
                     err.ToString().c_str());
        } else {
            ESP_LOGE(TAG, "Failed to get error handle");
        }
        destroy_tls();
        return Fail(err);
    }

    int sockfd = -1;
    if (esp_tls_get_conn_sockfd(tls_client_, &sockfd) != ESP_OK || sockfd < 0) {
        ESP_LOGE(TAG, "Failed to get socket fd");
        destroy_tls();
        return Fail(NetworkError::TlsFailed());
    }
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int err = errno;
        ESP_LOGE(TAG, "Failed to set socket non-blocking: errno=%d", err);
        destroy_tls();
        return Fail(NetworkError::FromErrno(err));
    }
    sockfd_ = sockfd;

    connected_ = true;
    xEventGroupClearBits(event_group_, ESP_SSL_EVENT_RECEIVE_TASK_EXIT);
    BaseType_t rc = xTaskCreate(
        [](void* arg) {
            TicketSsl* ssl = (TicketSsl*)arg;
            ssl->ReceiveTask();
            xEventGroupSetBits(ssl->event_group_, ESP_SSL_EVENT_RECEIVE_TASK_EXIT);
            vTaskDelete(NULL);
        },
        "ssl_receive", 4096, this, 1, &receive_task_handle_);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create receive task");
        connected_ = false;
        receive_task_handle_ = nullptr;
        destroy_tls();
        return Fail(NetworkError::FromErrno(ENOMEM));
    }
    return {};
}

void TicketSsl::Disconnect() {
    bool was_connected = connected_.exchange(false);
    if (tls_client_ == nullptr) {
        return;
    }

    // Use shutdown(), not close(). close() hands the fd number back to lwIP
    // immediately, and esp_tls_conn_destroy() below closes the same number
    // again. shutdown() still wakes ReceiveTask, but keeps the fd so
    // esp_tls_conn_destroy() closes it exactly once.
    if (sockfd_ >= 0) {
        shutdown(sockfd_, SHUT_RDWR);
    }

    if (receive_task_handle_ != nullptr) {
        auto bits = xEventGroupWaitBits(event_group_, ESP_SSL_EVENT_RECEIVE_TASK_EXIT, pdFALSE,
                                        pdFALSE, pdMS_TO_TICKS(10000));
        if (!(bits & ESP_SSL_EVENT_RECEIVE_TASK_EXIT)) {
            ESP_LOGE(TAG, "Failed to wait for receive task exit");
        }
        receive_task_handle_ = nullptr;
    }

    {
        // Waits for a TLS call in a concurrent Send() to return
        std::lock_guard<std::mutex> lock(tls_mutex_);
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = nullptr;
        sockfd_ = -1;
    }

    if (was_connected && disconnect_callback_) {
        disconnect_callback_();
    }
}

int TicketSsl::Send(const std::string& data) {
    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    size_t total_sent = 0;
    size_t data_size = data.size();
    const char* data_ptr = data.data();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(ESP_SSL_SEND_TIMEOUT_MS);
    TickType_t last_progress = xTaskGetTickCount();

    while (total_sent < data_size) {
        int ret;
        int sockfd;
        {
            std::lock_guard<std::mutex> lock(tls_mutex_);
            if (tls_client_ == nullptr || !connected_) {
                ESP_LOGE(TAG, "Not connected");
                return -1;
            }
            // After WANT_WRITE mbedTLS requires the same arguments on retry,
            // which holds because total_sent only advances on success.
            ret = esp_tls_conn_write(tls_client_, data_ptr + total_sent, data_size - total_sent);
            sockfd = sockfd_;
        }

        if (ret > 0) {
            total_sent += ret;
            last_progress = xTaskGetTickCount();
            continue;
        }

        if (ret != ESP_TLS_ERR_SSL_WANT_WRITE && ret != ESP_TLS_ERR_SSL_WANT_READ) {
            ESP_LOGE(TAG, "SSL send failed: ret=%d, errno=%d", ret, errno);
            return ret;
        }

        TickType_t elapsed = xTaskGetTickCount() - last_progress;
        if (elapsed >= timeout_ticks) {
            ESP_LOGE(TAG, "SSL send timeout, sent %u of %u bytes", (unsigned)total_sent,
                     (unsigned)data_size);
            return -1;
        }
        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE) {
            WaitSocket(sockfd, false, true, (timeout_ticks - elapsed) * portTICK_PERIOD_MS);
        } else {
            // The record mbedTLS waits for is read by ReceiveTask, so poll
            // instead of competing with it in select().
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    return total_sent;
}

void TicketSsl::ReceiveTask() {
    std::string data;
    bool want_write = false;
    while (connected_) {
        bool pending;
        {
            std::lock_guard<std::mutex> lock(tls_mutex_);
            pending = esp_tls_get_bytes_avail(tls_client_) > 0;
        }
        // Decrypted bytes may remain in mbedTLS when the record is larger than
        // the read buffer; the socket would not signal readability for them.
        if (!pending) {
            // Woken by incoming data, peer close or reset, or shutdown() in Disconnect()
            int ret = WaitSocket(sockfd_, true, want_write, -1);
            if (ret < 0 && errno != EINTR) {
                ESP_LOGE(TAG, "select failed: errno=%d", errno);
            }
            if (!connected_) {
                break;
            }
        }

        data.resize(1500);
        int ret;
        {
            std::lock_guard<std::mutex> lock(tls_mutex_);
            ret = esp_tls_conn_read(tls_client_, data.data(), data.size());
        }

        if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE) {
            want_write = ret == ESP_TLS_ERR_SSL_WANT_WRITE;
            continue;
        }
        want_write = false;

        if (ret <= 0) {
            // Passive disconnect: only report it, Disconnect() releases the TLS
            // context. exchange() keeps the callback to one call when
            // Disconnect() races.
            if (connected_.exchange(false)) {
                if (ret < 0) {
                    ESP_LOGE(TAG, "SSL receive failed: %d", ret);
                }
                if (disconnect_callback_) {
                    disconnect_callback_();
                }
            }
            break;
        }

        if (stream_callback_) {
            data.resize(ret);
            stream_callback_(data);
        }
    }
}
