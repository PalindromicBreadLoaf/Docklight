#include "Network.h"
#include <chrono>
#include <spdlog/spdlog.h>
#include <libultraship/libultraship.h>
#include "port/ThreadAffinity.h"

// MARK: - Public

void Network::Enable(const char* host, uint16_t port) {
#ifdef USE_NETWORKING
    if (isEnabled.load()) {
        return;
    }

    if (SDLNet_ResolveHost(&networkAddress, host, port) == -1) {
        SPDLOG_ERROR("[Network] SDLNet_ResolveHost: {}", SDLNet_GetError());
        return;
    }

    // First check if there is a thread running, if so, join it
    if (receiveThread.joinable()) {
        receiveThread.join();
    }

    isEnabled.store(true);
    receiveThread = std::thread(&Network::ReceiveFromServer, this);
#endif
}

void Network::Disable() {
    if (!isEnabled.exchange(false)) {
        return;
    }

    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

void Network::OnIncomingData(char payload[512]) {
}

void Network::OnIncomingJson(nlohmann::json payload) {
}

void Network::OnConnected() {
}

void Network::OnDisconnected() {
}

void Network::ProcessOutgoingPackets() {
}

void Network::SendDataToRemote(const char* payload) {
#ifdef USE_NETWORKING
    SPDLOG_DEBUG("[Network] Sending data: {}", payload);
    SDLNet_TCP_Send(networkSocket, payload, strlen(payload) + 1);
#endif
}

void Network::SendJsonToRemote(nlohmann::json payload) {
    SendDataToRemote(payload.dump().c_str());
}

// MARK: - Private

void Network::ReceiveFromServer() {
#ifdef USE_NETWORKING
    port_pinCurrentThread(PORT_ROLE_AUX);

    while (isEnabled.load()) {
        while (!isConnected.load() && isEnabled.load()) {
            SPDLOG_TRACE("[Network] Attempting to make connection to server...");
            networkSocket = SDLNet_TCP_Open(&networkAddress);

            if (networkSocket) {
                isConnected.store(true);
                receivedData.clear();
                SPDLOG_INFO("[Network] Connection to server established!");

                OnConnected();
                break;
            }

            // SDLNet_TCP_Open can fail immediately while a host is unavailable.
            // Back off so a failed connection does not consume an entire core.
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        if (!isEnabled.load()) {
            break;
        }

        SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
        if (!socketSet) {
            SPDLOG_ERROR("[Network] SDLNet_AllocSocketSet: {}", SDLNet_GetError());
        } else if (networkSocket && SDLNet_TCP_AddSocket(socketSet, networkSocket) == -1) {
            SPDLOG_ERROR("[Network] SDLNet_TCP_AddSocket: {}", SDLNet_GetError());
            SDLNet_FreeSocketSet(socketSet);
            socketSet = nullptr;
        }

        // Listen to socket messages
        while (socketSet && isConnected.load() && networkSocket && isEnabled.load()) {
            // we check first if socket has data, to not block in the TCP_Recv
            int socketsReady = SDLNet_CheckSockets(socketSet, 10);

            if (socketsReady == -1) {
                SPDLOG_ERROR("[Network] SDLNet_CheckSockets: {}", SDLNet_GetError());
                break;
            }

            // Always process outgoing packets
            ProcessOutgoingPackets();

            if (socketsReady == 0) {
                // No incoming data
                continue;
            }

            char remoteDataReceived[512];
            memset(remoteDataReceived, 0, sizeof(remoteDataReceived));
            int len = SDLNet_TCP_Recv(networkSocket, &remoteDataReceived, sizeof(remoteDataReceived));
            if (!len || !networkSocket || len == -1) {
                SPDLOG_ERROR("[Network] SDLNet_TCP_Recv: {}", SDLNet_GetError());
                break;
            }

            HandleRemoteData(remoteDataReceived);

            receivedData.append(remoteDataReceived, len);

            // Proess all complete packets
            size_t delimiterPos = receivedData.find('\0');
            while (delimiterPos != std::string::npos) {
                // Extract the complete packet until the delimiter
                std::string packet = receivedData.substr(0, delimiterPos);
                // Remove the packet (including the delimiter) from the received data
                receivedData.erase(0, delimiterPos + 1);
                HandleRemoteJson(packet);
                // Find the next delimiter
                delimiterPos = receivedData.find('\0');
            }
        }

        if (socketSet) {
            SDLNet_FreeSocketSet(socketSet);
        }

        if (isConnected.exchange(false)) {
            SDLNet_TCP_Close(networkSocket);
            networkSocket = nullptr;
            receivedData.clear();
            OnDisconnected();
            if (isEnabled.load()) {
                SPDLOG_INFO("[Network] Connection ended; reconnecting...");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
    }

    SPDLOG_INFO("[Network] Ending receiving thread...");
#endif
}

void Network::HandleRemoteData(char payload[512]) {
    OnIncomingData(payload);
}

void Network::HandleRemoteJson(std::string payload) {
    SPDLOG_DEBUG("[Network] Received json: {}", payload);
    nlohmann::json jsonPayload;
    try {
        jsonPayload = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Failed to parse json: \n{}\n{}\n", payload, e.what());
        return;
    }

    OnIncomingJson(jsonPayload);
}
