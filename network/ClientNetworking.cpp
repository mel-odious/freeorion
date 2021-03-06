#define WIN32_LEAN_AND_MEAN

#include "ClientNetworking.h"

#include "Networking.h"
#include "../util/Logger.h"
#include "../util/MultiplayerCommon.h"
#include "../util/OptionsDB.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/bind.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/thread.hpp>

#include <thread>

using boost::asio::ip::tcp;
using namespace Networking;

namespace {
    const bool TRACE_EXECUTION = false;

    /** A simple client that broadcasts UDP datagrams on the local network for
        FreeOrion servers, and reports any it finds. */
    class ServerDiscoverer {
    public:
        typedef ClientNetworking::ServerList ServerList;

        ServerDiscoverer(boost::asio::io_service& io_service) :
            m_io_service(&io_service),
            m_timer(io_service),
            m_socket(io_service),
            m_recv_buf(),
            m_receive_successful(false),
            m_server_name()
        {}

        const ServerList& Servers() const
        { return m_servers; }

        void DiscoverServers() {
            using namespace boost::asio::ip;
            udp::resolver resolver(*m_io_service);
            udp::resolver::query query(udp::v4(), "255.255.255.255",
                                       std::to_string(Networking::DiscoveryPort()),
                                       resolver_query_base::address_configured |
                                       resolver_query_base::numeric_service);
            udp::resolver::iterator end_it;
            for (udp::resolver::iterator it = resolver.resolve(query); it != end_it; ++it) {
                udp::endpoint receiver_endpoint = *it;

                m_socket.close();
                m_socket.open(udp::v4());
                m_socket.set_option(boost::asio::socket_base::broadcast(true));

                m_socket.send_to(boost::asio::buffer(DISCOVERY_QUESTION),
                                 receiver_endpoint);

                m_socket.async_receive_from(
                    boost::asio::buffer(m_recv_buf),
                    m_sender_endpoint,
                    boost::bind(&ServerDiscoverer::HandleReceive,
                                this,
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
                m_timer.expires_from_now(std::chrono::seconds(2));
                m_timer.async_wait(boost::bind(&ServerDiscoverer::CloseSocket, this));
                m_io_service->run();
                m_io_service->reset();
                if (m_receive_successful) {
                    boost::asio::ip::address address = m_server_name == "localhost" ?
                        boost::asio::ip::address::from_string("127.0.0.1") :
                        m_sender_endpoint.address();
                    m_servers.push_back({address, m_server_name});
                }
                m_receive_successful = false;
                m_server_name = "";
            }
        }

    private:
        void HandleReceive(const boost::system::error_code& error, std::size_t length) {
            if (error == boost::asio::error::message_size) {
                m_socket.async_receive_from(
                    boost::asio::buffer(m_recv_buf),
                    m_sender_endpoint,
                    boost::bind(&ServerDiscoverer::HandleReceive,
                                this,
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
            } else if (!error) {
                std::string buffer_string(m_recv_buf.begin(), m_recv_buf.begin() + length);
                if (boost::algorithm::starts_with(buffer_string, DISCOVERY_ANSWER)) {
                    m_receive_successful = true;
                    m_server_name =
                        boost::algorithm::erase_first_copy(buffer_string, DISCOVERY_ANSWER);
                    if (m_server_name == boost::asio::ip::host_name())
                        m_server_name = "localhost";
                    m_timer.cancel();
                    m_socket.close();
                }
            }
        }

        void CloseSocket()
        { m_socket.close(); }

        boost::asio::io_service*       m_io_service;
        boost::asio::high_resolution_timer m_timer;
        boost::asio::ip::udp::socket   m_socket;

        std::array<char, 1024> m_recv_buf;

        boost::asio::ip::udp::endpoint m_sender_endpoint;
        bool                           m_receive_successful;
        std::string                    m_server_name;
        ServerList                     m_servers;
    };
}

////////////////////////////////////////////////
// ClientNetworking
////////////////////////////////////////////////
ClientNetworking::ClientNetworking() :
    m_player_id(Networking::INVALID_PLAYER_ID),
    m_host_player_id(Networking::INVALID_PLAYER_ID),
    m_io_service(),
    m_socket(m_io_service),
    m_incoming_messages(m_mutex),
    m_rx_connected(false),
    m_tx_connected(false)
{}

bool ClientNetworking::IsConnected() const {
    boost::mutex::scoped_lock lock(m_mutex);
    return m_rx_connected && m_tx_connected;
}

bool ClientNetworking::IsRxConnected() const {
    boost::mutex::scoped_lock lock(m_mutex);
    return m_rx_connected;
}

bool ClientNetworking::IsTxConnected() const {
    boost::mutex::scoped_lock lock(m_mutex);
    return m_tx_connected;
}

bool ClientNetworking::MessageAvailable() const
{ return !m_incoming_messages.Empty(); }

int ClientNetworking::PlayerID() const
{ return m_player_id; }

int ClientNetworking::HostPlayerID() const
{ return m_host_player_id; }

bool ClientNetworking::PlayerIsHost(int player_id) const {
    if (player_id == Networking::INVALID_PLAYER_ID)
        return false;
    return player_id == m_host_player_id;
}

ClientNetworking::ServerList ClientNetworking::DiscoverLANServers() {
    if (!IsConnected())
        return ServerList();
    ServerDiscoverer discoverer(m_io_service);
    discoverer.DiscoverServers();
    return discoverer.Servers();
}

bool ClientNetworking::ConnectToServer(
    const std::string& ip_address,
    std::chrono::milliseconds timeout/* = std::chrono::seconds(10)*/)
{
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start_time = Clock::now();
    auto deadline = start_time + timeout;

    using namespace boost::asio::ip;
    tcp::resolver resolver(m_io_service);
    tcp::resolver::query query(ip_address,
                               std::to_string(Networking::MessagePort()),
                               boost::asio::ip::resolver_query_base::numeric_service);

    tcp::resolver::iterator end_it;

    DebugLogger() << "Attempt to connect to server at one of these addresses:";
    for (tcp::resolver::iterator it = resolver.resolve(query); it != end_it; ++it) {
        DebugLogger() << "  tcp::resolver::iterator host_name: " << it->host_name()
                      << "  address: " << it->endpoint().address()
                      << "  port: " << it->endpoint().port();
    }

    try {
        while(!IsConnected() && Clock::now() < deadline) {
            for (tcp::resolver::iterator it = resolver.resolve(query); it != end_it; ++it) {
                m_socket.close();

                m_socket.async_connect(*it, boost::bind(&ClientNetworking::HandleConnection, this,
                                                        &it,
                                                        boost::asio::placeholders::error));
                m_io_service.run();
                m_io_service.reset();

                auto connection_time = Clock::now() - start_time;

                if (IsConnected()) {
                    DebugLogger() << "Connected to server at host_name: " << it->host_name()
                                  << "  address: " << it->endpoint().address()
                                  << "  port: " << it->endpoint().port();

                    //DebugLogger() << "ConnectToServer() : Client using "
                    //              << ((GetOptionsDB().Get<bool>("binary-serialization")) ? "binary": "xml")
                    //              << " serialization.";

                    // Prepare the socket

                    // linger option has different meanings on different platforms.  It affects the
                    // behavior of the socket.close().  It can do the following:
                    // - close both send and receive immediately,
                    // - finish sending any pending send packets and wait up to SOCKET_LINGER_TIME for
                    // ACKs,
                    // - finish sending pending sent packets and wait up to SOCKET_LINGER_TIME for ACKs
                    // and for the other side of the connection to close,
                    // linger may/may not cause close() to block until the linger time has elapsed.
                    m_socket.set_option(boost::asio::socket_base::linger(true, SOCKET_LINGER_TIME));

                    // keep alive is an OS dependent option that will keep the TCP connection alive and
                    // then deliver an OS dependent error when/if the other side of the connection
                    // times out or closes.
                    m_socket.set_option(boost::asio::socket_base::keep_alive(true));
                    DebugLogger() << "Connecting to server took "
                                  << std::chrono::duration_cast<std::chrono::milliseconds>(connection_time).count() << " ms.";

                    DebugLogger() << "ConnectToServer() : starting networking thread";
                    auto self = shared_from_this();
                    boost::thread(boost::bind(&ClientNetworking::NetworkingThread, this, self));
                    break;
                } else {
                    if (TRACE_EXECUTION)
                        DebugLogger() << "Failed to connect to host_name: " << it->host_name()
                                      << "  address: " << it->endpoint().address()
                                      << "  port: " << it->endpoint().port();
                    if (timeout < connection_time) {
                        ErrorLogger() << "Timed out ("
                                      << std::chrono::duration_cast<std::chrono::milliseconds>(connection_time).count() << " ms."
                                      << ") attempting to connect to server.";
                    }
                }
            }
        }
        if (!IsConnected())
            DebugLogger() << "ConnectToServer() : failed to connect to server.";

    } catch (const std::exception& e) {
        ErrorLogger() << "ConnectToServer() : unable to connect to server at "
                      << ip_address << " due to exception: " << e.what();
    }
    return IsConnected();
}

bool ClientNetworking::ConnectToLocalHostServer(
    std::chrono::milliseconds timeout/* = std::chrono::seconds(10)*/)
{
    bool retval = false;
#if FREEORION_WIN32
    try {
#endif
        retval = ConnectToServer("127.0.0.1", timeout);
#if FREEORION_WIN32
    } catch (const boost::system::system_error& e) {
        if (e.code().value() != WSAEADDRNOTAVAIL)
            throw;
    }
#endif
    return retval;
}

void ClientNetworking::DisconnectFromServer() {
    bool is_open(false);

    { // Create a scope for the mutex
        boost::mutex::scoped_lock lock(m_mutex);
        is_open = m_rx_connected || m_tx_connected;
    }

    if (is_open)
        m_io_service.post(boost::bind(&ClientNetworking::DisconnectFromServerImpl, this));
}

void ClientNetworking::SetPlayerID(int player_id) {
    DebugLogger() << "ClientNetworking::SetPlayerID: player id set to: " << player_id;
    m_player_id = player_id;
}

void ClientNetworking::SetHostPlayerID(int host_player_id)
{ m_host_player_id = host_player_id; }

void ClientNetworking::SendMessage(Message message) {
    if (!IsTxConnected()) {
        ErrorLogger() << "ClientNetworking::SendMessage can't send message when not transmit connected";
        return;
    }
    if (TRACE_EXECUTION)
        DebugLogger() << "ClientNetworking::SendMessage() : sending message " << message;
    m_io_service.post(boost::bind(&ClientNetworking::SendMessageImpl, this, message));
}

void ClientNetworking::GetMessage(Message& message) {
    if (!MessageAvailable()) {
        ErrorLogger() << "ClientNetworking::GetMessage can't get message if none available";
        return;
    }
    m_incoming_messages.PopFront(message);
    if (TRACE_EXECUTION)
        DebugLogger() << "ClientNetworking::GetMessage() : received message "
                      << message;
}

void ClientNetworking::SendSynchronousMessage(Message message, Message& response_message) {
    if (TRACE_EXECUTION)
        DebugLogger() << "ClientNetworking::SendSynchronousMessage : sending message "
                               << message;
    SendMessage(message);
    // note that this is a blocking operation
    m_incoming_messages.EraseFirstSynchronousResponse(response_message);
    if (TRACE_EXECUTION)
        DebugLogger() << "ClientNetworking::SendSynchronousMessage : received "
                               << "response message " << response_message;
}

void ClientNetworking::HandleConnection(tcp::resolver::iterator* it,
                                        const boost::system::error_code& error)
{
    if (error) {
        if (TRACE_EXECUTION)
            DebugLogger() << "ClientNetworking::HandleConnection : connection "
                          << "error #"<<error.value()<<" \"" << error.message() << "\""
                          << "... retrying";
    } else {
        if (TRACE_EXECUTION)
            DebugLogger() << "ClientNetworking::HandleConnection : connected";

        boost::mutex::scoped_lock lock(m_mutex);
        m_rx_connected = true;
        m_tx_connected = true;
    }
}

void ClientNetworking::HandleException(const boost::system::system_error& error) {
    if (error.code() == boost::asio::error::eof) {
        DebugLogger() << "Client connection disconnected by EOF from server.";
        m_socket.close();
    }
    else if (error.code() == boost::asio::error::connection_reset)
        DebugLogger() << "Client connection disconnected, due to connection reset from server.";
    else if (error.code() == boost::asio::error::operation_aborted)
        DebugLogger() << "Client connection closed by client.";
    else {
        ErrorLogger() << "ClientNetworking::NetworkingThread() : Networking thread will be terminated "
                      << "due to unhandled exception error #" << error.code().value() << " \""
                      << error.code().message() << "\"";
    }
}

void ClientNetworking::NetworkingThread(std::shared_ptr<ClientNetworking>& self) {
    auto protect_from_destruction_in_other_thread = self;
    try {
        if (!m_outgoing_messages.empty())
            AsyncWriteMessage();
        AsyncReadMessage(protect_from_destruction_in_other_thread);
        m_io_service.run();
    } catch (const boost::system::system_error& error) {
        HandleException(error);
    }
    m_incoming_messages.Clear();
    m_outgoing_messages.clear();
    m_io_service.reset();
    { // Mutex scope
        boost::mutex::scoped_lock lock(m_mutex);
        m_rx_connected = false;
        m_tx_connected = false;
    }
    if (TRACE_EXECUTION)
        DebugLogger() << "ClientNetworking::NetworkingThread() : Networking thread terminated.";
}

void ClientNetworking::HandleMessageBodyRead(const std::shared_ptr<ClientNetworking>& keep_alive,
                                             boost::system::error_code error, std::size_t bytes_transferred)
{
    if (error)
        throw boost::system::system_error(error);

    assert(static_cast<int>(bytes_transferred) <= m_incoming_header[4]);
    if (static_cast<int>(bytes_transferred) == m_incoming_header[4]) {
        m_incoming_messages.PushBack(m_incoming_message);
        AsyncReadMessage(keep_alive);
    }
}

void ClientNetworking::HandleMessageHeaderRead(const std::shared_ptr<ClientNetworking>& keep_alive,
                                               boost::system::error_code error, std::size_t bytes_transferred)
{
    if (error)
        throw boost::system::system_error(error);
    assert(bytes_transferred <= Message::HeaderBufferSize);
    if (bytes_transferred != Message::HeaderBufferSize)
        return;

    BufferToHeader(m_incoming_header, m_incoming_message);
    m_incoming_message.Resize(m_incoming_header[4]);
    // Intentionally not checked for open.  We expect (header, body) pairs.
    boost::asio::async_read(
        m_socket,
        boost::asio::buffer(m_incoming_message.Data(), m_incoming_message.Size()),
        boost::bind(&ClientNetworking::HandleMessageBodyRead,
                    this, keep_alive,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void ClientNetworking::AsyncReadMessage(const std::shared_ptr<ClientNetworking>& keep_alive) {
    // If keep_alive's count < 2 the networking thread is orphaned so shut down
    if (keep_alive.use_count() < 2)
        DisconnectFromServerImpl();

    if (m_socket.is_open())
        boost::asio::async_read(
            m_socket, boost::asio::buffer(m_incoming_header),
            boost::bind(&ClientNetworking::HandleMessageHeaderRead, this, keep_alive,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
}

void ClientNetworking::HandleMessageWrite(boost::system::error_code error, std::size_t bytes_transferred) {
    if (error) {
        throw boost::system::system_error(error);
        return;
    }

    assert(static_cast<int>(bytes_transferred) <= static_cast<int>(Message::HeaderBufferSize) + m_outgoing_header[4]);
    if (static_cast<int>(bytes_transferred) != static_cast<int>(Message::HeaderBufferSize) + m_outgoing_header[4])
        return;

    m_outgoing_messages.pop_front();
    if (!m_outgoing_messages.empty())
        AsyncWriteMessage();

    // Check if finished sending last pending write while shutting down.
    else {
        bool should_shutdown(false);
        { // Scope for the mutex
            boost::mutex::scoped_lock lock(m_mutex);
            should_shutdown = !m_tx_connected;
        }
        if (should_shutdown) {
            DisconnectFromServerImpl();
        }
    }
}

void ClientNetworking::AsyncWriteMessage() {
    if (!m_socket.is_open()) {
        ErrorLogger() << "Socket is closed. Dropping message.";
        return;
    }

    HeaderToBuffer(m_outgoing_messages.front(), m_outgoing_header);
    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(boost::asio::buffer(m_outgoing_header));
    buffers.push_back(boost::asio::buffer(m_outgoing_messages.front().Data(),
                                          m_outgoing_messages.front().Size()));
    boost::asio::async_write(m_socket, buffers,
                             boost::bind(&ClientNetworking::HandleMessageWrite, this,
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

void ClientNetworking::SendMessageImpl(Message message) {
    bool start_write = m_outgoing_messages.empty();
    m_outgoing_messages.push_back(Message());
    swap(m_outgoing_messages.back(), message);
    if (start_write)
        AsyncWriteMessage();
}

void ClientNetworking::DisconnectFromServerImpl() {
    // Depending behavior of linger on OS's of the sending and receiving machines this call to close could
    // - immediately disconnect both send and receive channels
    // - immediately disconnect send, but continue receiving until all pending sent packets are
    //   received and acknowledged.
    // - send pending packets and wait for the receive side to terminate the connection.

    // The shutdown steps:
    // 1. Finish sending pending tx messages.  These may include final panic messages
    // 2. After the final tx call DisconnectFromServerImpl again from the tx handler.
    // 3. shutdown the connection
    // 4. server end acknowledges with a 0 length packet
    // 5. close the connection in the rx handler

    // Stop sending new packets
    { // Scope for the mutex
        boost::mutex::scoped_lock lock(m_mutex);
        m_tx_connected = false;
        m_rx_connected = m_socket.is_open();
    }

    if (!m_outgoing_messages.empty()) {
        return;
    }

    // Note: m_socket.is_open() may be independently true/false on each of these checks.
    if (m_socket.is_open())
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
}
