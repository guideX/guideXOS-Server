#ifndef GXOS_VNC_SERVER_H
#define GXOS_VNC_SERVER_H

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace gxos {
namespace vnc {

/// VNC server that exposes compositor framebuffer over network
/// Allows VM/remote clients to view the GUI
class VncServer {
public:
    /// Start VNC server on specified port (default 5900)
    static bool Start(uint16_t port = 5900);
    
    /// Stop VNC server
    static void Stop();
    
    /// Check if server is running
    static bool IsRunning();
    
    /// Update framebuffer data (called by compositor)
    static void UpdateFramebuffer(const uint8_t* pixels, int width, int height, int stride);
    
    /// Set server password (optional)
    static void SetPassword(const std::string& password);
    
    /// Get current client count
    static int GetClientCount();

private:
    static void ServerThread();
    static void HandleClient(int clientSocket);
    
    static std::atomic<bool> s_running;
    static std::atomic<int> s_clientCount;
    static std::thread s_serverThread;
    static uint16_t s_port;
    static std::string s_password;
    
    // Framebuffer data
    static std::vector<uint8_t> s_framebuffer;
    static int s_fbWidth;
    static int s_fbHeight;
    static int s_fbStride;
    static std::mutex s_fbMutex;
    static std::atomic<bool> s_fbDirty;
};

} // namespace vnc
} // namespace gxos

#endif // GXOS_VNC_SERVER_H
