#include "vnc_server.h"
#include "logger.h"
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

namespace gxos {
namespace vnc {

// RFB (Remote Framebuffer Protocol) constants
constexpr uint8_t RFB_VERSION_3_8[] = "RFB 003.008\n";
constexpr uint8_t SECURITY_TYPE_NONE = 1;
constexpr uint8_t SECURITY_RESULT_OK[4] = {0, 0, 0, 0};

// Static member initialization
std::atomic<bool> VncServer::s_running{false};
std::atomic<int> VncServer::s_clientCount{0};
std::thread VncServer::s_serverThread;
uint16_t VncServer::s_port = 5900;
std::string VncServer::s_password;
std::vector<uint8_t> VncServer::s_framebuffer;
int VncServer::s_fbWidth = 1024;
int VncServer::s_fbHeight = 768;
int VncServer::s_fbStride = 1024 * 4; // RGBA
std::mutex VncServer::s_fbMutex;
std::atomic<bool> VncServer::s_fbDirty{false};

bool VncServer::Start(uint16_t port) {
if (s_running.load()) {
    Logger::write(LogLevel::Warn, "VNC server already running");
    return false;
}
    
    s_port = port;
    s_running.store(true);
    s_clientCount.store(0);
    
    // Initialize framebuffer
    {
        std::lock_guard<std::mutex> lock(s_fbMutex);
        s_framebuffer.resize(s_fbWidth * s_fbHeight * 4, 0);
    }
    
    // Start server thread
    s_serverThread = std::thread(ServerThread);
    
    Logger::write(LogLevel::Info, "VNC server started on port " + std::to_string(port));
    return true;
}

void VncServer::Stop() {
    if (!s_running.load()) {
        return;
    }
    
    s_running.store(false);
    
    if (s_serverThread.joinable()) {
        s_serverThread.join();
    }
    
    Logger::write(LogLevel::Info, "VNC server stopped");
}

bool VncServer::IsRunning() {
    return s_running.load();
}

void VncServer::UpdateFramebuffer(const uint8_t* pixels, int width, int height, int stride) {
    std::lock_guard<std::mutex> lock(s_fbMutex);
    
    if (width != s_fbWidth || height != s_fbHeight) {
        s_fbWidth = width;
        s_fbHeight = height;
        s_framebuffer.resize(width * height * 4);
    }
    
    // Copy pixel data
    if (stride == width * 4) {
        // Direct copy if stride matches
        std::memcpy(s_framebuffer.data(), pixels, width * height * 4);
    } else {
        // Row-by-row copy if stride differs
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                s_framebuffer.data() + y * width * 4,
                pixels + y * stride,
                width * 4
            );
        }
    }
    
    s_fbDirty.store(true);
}

void VncServer::SetPassword(const std::string& password) {
    s_password = password;
}

int VncServer::GetClientCount() {
    return s_clientCount.load();
}

void VncServer::ServerThread() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::write(LogLevel::Error, "VNC: WSAStartup failed");
        return;
    }
#endif

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        Logger::write(LogLevel::Error, "VNC: Failed to create socket");
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    // Bind to port
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(s_port);
    
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        Logger::write(LogLevel::Error, "VNC: Failed to bind to port " + std::to_string(s_port));
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    
    // Listen for connections
    if (listen(serverSocket, 5) == SOCKET_ERROR) {
        Logger::write(LogLevel::Error, "VNC: Failed to listen");
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    
    Logger::write(LogLevel::Info, "VNC: Listening on port " + std::to_string(s_port));
    
    // Accept connections
    while (s_running.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);
        
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int result = select((int)serverSocket + 1, &readSet, nullptr, nullptr, &timeout);
        if (result > 0) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
            
            if (clientSocket != INVALID_SOCKET) {
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
                Logger::write(LogLevel::Info, "VNC: Client connected from " + std::string(clientIP));
                
                // Handle client in new thread
                std::thread clientThread([clientSocket]() {
                    s_clientCount.fetch_add(1);
                    HandleClient(clientSocket);
                    s_clientCount.fetch_sub(1);
                });
                clientThread.detach();
            }
        }
    }
    
    closesocket(serverSocket);
#ifdef _WIN32
    WSACleanup();
#endif
}

void VncServer::HandleClient(int clientSocket) {
    // Send RFB version
    send(clientSocket, (const char*)RFB_VERSION_3_8, 12, 0);
    
    // Receive client version
    char clientVersion[12];
    if (recv(clientSocket, clientVersion, 12, 0) != 12) {
        Logger::write(LogLevel::Warn, "VNC: Client version handshake failed");
        closesocket(clientSocket);
        return;
    }
    
    // Send security type (None)
    uint8_t secTypes[2] = {1, SECURITY_TYPE_NONE};
    send(clientSocket, (const char*)secTypes, 2, 0);
    
    // Receive client security choice
    uint8_t secChoice;
    if (recv(clientSocket, (char*)&secChoice, 1, 0) != 1) {
        Logger::write(LogLevel::Warn, "VNC: Client security handshake failed");
        closesocket(clientSocket);
        return;
    }
    
    // Send security result (OK)
    send(clientSocket, (const char*)SECURITY_RESULT_OK, 4, 0);
    
    // Receive ClientInit
    uint8_t clientInit;
    if (recv(clientSocket, (char*)&clientInit, 1, 0) != 1) {
        Logger::write(LogLevel::Warn, "VNC: ClientInit failed");
        closesocket(clientSocket);
        return;
    }
    
    // Send ServerInit
    struct {
        uint16_t width;
        uint16_t height;
        uint8_t pixelFormat[16]; // Pixel format structure
        uint32_t nameLength;
        char name[12];
    } serverInit;
    
    {
        std::lock_guard<std::mutex> lock(s_fbMutex);
        serverInit.width = htons(s_fbWidth);
        serverInit.height = htons(s_fbHeight);
    }
    
    // Pixel format: 32-bit RGBA
    serverInit.pixelFormat[0] = 32; // bits-per-pixel
    serverInit.pixelFormat[1] = 24; // depth
    serverInit.pixelFormat[2] = 0;  // big-endian-flag (0 = little endian)
    serverInit.pixelFormat[3] = 1;  // true-color-flag
    serverInit.pixelFormat[4] = 0;  // red-max high byte
    serverInit.pixelFormat[5] = 255; // red-max low byte
    serverInit.pixelFormat[6] = 0;  // green-max high byte
    serverInit.pixelFormat[7] = 255; // green-max low byte
    serverInit.pixelFormat[8] = 0;  // blue-max high byte
    serverInit.pixelFormat[9] = 255; // blue-max low byte
    serverInit.pixelFormat[10] = 16; // red-shift
    serverInit.pixelFormat[11] = 8;  // green-shift
    serverInit.pixelFormat[12] = 0;  // blue-shift
    serverInit.pixelFormat[13] = 0;  // padding
    serverInit.pixelFormat[14] = 0;  // padding
    serverInit.pixelFormat[15] = 0;  // padding
    
    serverInit.nameLength = htonl(11);
    std::memcpy(serverInit.name, "guideXOS VM", 11);
    
    send(clientSocket, (const char*)&serverInit, sizeof(serverInit), 0);
    
    Logger::write(LogLevel::Info, "VNC: Client initialized, entering main loop");
    
    // Main message loop
    while (s_running.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);
        
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        
        int result = select((int)clientSocket + 1, &readSet, nullptr, nullptr, &timeout);
        if (result > 0) {
            // Receive client message
            uint8_t msgType;
            if (recv(clientSocket, (char*)&msgType, 1, 0) != 1) {
                break;
            }
            
            // Handle different message types
            switch (msgType) {
                case 0: // SetPixelFormat
                {
                    uint8_t pad[3];
                    uint8_t pixelFormat[16];
                    recv(clientSocket, (char*)pad, 3, 0);
                    recv(clientSocket, (char*)pixelFormat, 16, 0);
                    break;
                }
                case 2: // SetEncodings
                {
                    uint8_t pad;
                    uint16_t numEncodings;
                    recv(clientSocket, (char*)&pad, 1, 0);
                    recv(clientSocket, (char*)&numEncodings, 2, 0);
                    numEncodings = ntohs(numEncodings);
                    std::vector<int32_t> encodings(numEncodings);
                    recv(clientSocket, (char*)encodings.data(), numEncodings * 4, 0);
                    break;
                }
                case 3: // FramebufferUpdateRequest
                {
                    uint8_t incremental;
                    uint16_t x, y, w, h;
                    recv(clientSocket, (char*)&incremental, 1, 0);
                    recv(clientSocket, (char*)&x, 2, 0);
                    recv(clientSocket, (char*)&y, 2, 0);
                    recv(clientSocket, (char*)&w, 2, 0);
                    recv(clientSocket, (char*)&h, 2, 0);
                    
                    // Send framebuffer update if dirty or full update requested
                    if (!incremental || s_fbDirty.load()) {
                        std::lock_guard<std::mutex> lock(s_fbMutex);
                        
                        // FramebufferUpdate message header
                        struct {
                            uint8_t msgType;
                            uint8_t padding;
                            uint16_t numRects;
                        } updateHeader;
                        updateHeader.msgType = 0;
                        updateHeader.padding = 0;
                        updateHeader.numRects = htons(1);
                        send(clientSocket, (const char*)&updateHeader, 4, 0);
                        
                        // Rectangle header
                        struct {
                            uint16_t x;
                            uint16_t y;
                            uint16_t w;
                            uint16_t h;
                            int32_t encoding; // 0 = Raw
                        } rectHeader;
                        rectHeader.x = 0;
                        rectHeader.y = 0;
                        rectHeader.w = htons(s_fbWidth);
                        rectHeader.h = htons(s_fbHeight);
                        rectHeader.encoding = 0; // Raw encoding
                        send(clientSocket, (const char*)&rectHeader, 12, 0);
                        
                        // Send raw pixel data
                        send(clientSocket, (const char*)s_framebuffer.data(), s_framebuffer.size(), 0);
                        
                        s_fbDirty.store(false);
                    }
                    break;
                }
                case 4: // KeyEvent
                {
                    uint8_t downFlag;
                    uint16_t padding;
                    uint32_t key;
                    recv(clientSocket, (char*)&downFlag, 1, 0);
                    recv(clientSocket, (char*)&padding, 2, 0);
                    recv(clientSocket, (char*)&key, 4, 0);
                    // TODO: Forward key events to compositor
                    break;
                }
                case 5: // PointerEvent
                {
                    uint8_t buttonMask;
                    uint16_t x, y;
                    recv(clientSocket, (char*)&buttonMask, 1, 0);
                    recv(clientSocket, (char*)&x, 2, 0);
                    recv(clientSocket, (char*)&y, 2, 0);
                    // TODO: Forward pointer events to compositor
                    break;
                }
                default:
                    Logger::write(LogLevel::Warn, "VNC: Unknown message type " + std::to_string(msgType));
                    break;
            }
        }
    }
    
    Logger::write(LogLevel::Info, "VNC: Client disconnected");
    closesocket(clientSocket);
}

} // namespace vnc
} // namespace gxos
