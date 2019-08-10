#include <AK/StringBuilder.h>
#include <Kernel/FileSystem/FileDescription.h>
#include <Kernel/FileSystem/VirtualFileSystem.h>
#include <Kernel/Net/LocalSocket.h>
#include <Kernel/Process.h>
#include <Kernel/UnixTypes.h>
#include <LibC/errno_numbers.h>

//#define DEBUG_LOCAL_SOCKET

void LocalSocket::for_each(Function<void(LocalSocket&)> callback)
{
    LOCKER(sockets_by_fd().lock());
    for (auto it : sockets_by_fd().resource())
        callback(*it.value);
}

Lockable<HashMap<FileDescription*, LocalSocket*>>& LocalSocket::sockets_by_fd()
{
    static Lockable<HashMap<FileDescription*, LocalSocket*>>* s_map;
    if (!s_map)
        s_map = new Lockable<HashMap<FileDescription*, LocalSocket*>>;
    return *s_map;
}

NonnullRefPtr<LocalSocket> LocalSocket::create(int type)
{
    return adopt(*new LocalSocket(type));
}

LocalSocket::LocalSocket(int type)
    : Socket(AF_LOCAL, type, 0)
{
#ifdef DEBUG_LOCAL_SOCKET
    kprintf("%s(%u) LocalSocket{%p} created with type=%u\n", current->process().name().characters(), current->pid(), this, type);
#endif
}

LocalSocket::~LocalSocket()
{
    LOCKER(sockets_by_fd().lock());
    sockets_by_fd().resource().remove(m_file);
}

bool LocalSocket::get_local_address(sockaddr* address, socklen_t* address_size)
{
    // FIXME: Look into what fallback behavior we should have here.
    if (*address_size != sizeof(sockaddr_un))
        return false;
    memcpy(address, &m_address, sizeof(sockaddr_un));
    *address_size = sizeof(sockaddr_un);
    return true;
}

bool LocalSocket::get_peer_address(sockaddr* address, socklen_t* address_size)
{
    return get_local_address(address, address_size);
}

KResult LocalSocket::bind(const sockaddr* address, socklen_t address_size)
{
    ASSERT(setup_state() == SetupState::Unstarted);
    if (address_size != sizeof(sockaddr_un))
        return KResult(-EINVAL);
    if (address->sa_family != AF_LOCAL)
        return KResult(-EINVAL);

    const sockaddr_un& local_address = *reinterpret_cast<const sockaddr_un*>(address);
    char safe_address[sizeof(local_address.sun_path) + 1] = { 0 };
    memcpy(safe_address, local_address.sun_path, sizeof(local_address.sun_path));

#ifdef DEBUG_LOCAL_SOCKET
    kprintf("%s(%u) LocalSocket{%p} bind(%s)\n", current->process().name().characters(), current->pid(), this, safe_address);
#endif

    auto result = VFS::the().open(safe_address, O_CREAT | O_EXCL, S_IFSOCK | 0666, current->process().current_directory());
    if (result.is_error()) {
        if (result.error() == -EEXIST)
            return KResult(-EADDRINUSE);
        return result.error();
    }
    {
        LOCKER(sockets_by_fd().lock());
        auto& sockets = sockets_by_fd().resource();
        sockets.remove(m_file);
        m_file = move(result.value());
        ASSERT(!sockets.contains(m_file));
        sockets.set(m_file, this);
    }

    ASSERT(m_file->inode());
    m_file->inode()->bind_socket(*this);

    m_address = local_address;
    m_bound = true;
    set_setup_state(SetupState::Completed);
    return KSuccess;
}

KResult LocalSocket::connect(FileDescription& description, const sockaddr* address, socklen_t address_size, ShouldBlock)
{
    ASSERT(!m_bound);
    if (address_size != sizeof(sockaddr_un))
        return KResult(-EINVAL);
    if (address->sa_family != AF_LOCAL)
        return KResult(-EINVAL);

    const sockaddr_un& local_address = *reinterpret_cast<const sockaddr_un*>(address);
    char safe_address[sizeof(local_address.sun_path) + 1] = { 0 };
    memcpy(safe_address, local_address.sun_path, sizeof(local_address.sun_path));

#ifdef DEBUG_LOCAL_SOCKET
    kprintf("%s(%u) LocalSocket{%p} connect(%s)\n", current->process().name().characters(), current->pid(), this, safe_address);
#endif

    auto description_or_error = VFS::the().open(safe_address, 0, 0, current->process().current_directory());
    if (description_or_error.is_error())
        return KResult(-ECONNREFUSED);

    {
        LOCKER(sockets_by_fd().lock());
        auto& sockets = sockets_by_fd().resource();
        sockets.remove(m_file);
        ASSERT(!sockets.contains(m_file));
        m_file = move(description_or_error.value());
        sockets.set(m_file, this);
    }

    ASSERT(m_file->inode());
    if (!m_file->inode()->socket())
        return KResult(-ECONNREFUSED);

    m_address = local_address;

    auto peer = m_file->inode()->socket();
    auto result = peer->queue_connection_from(*this);
    if (result.is_error())
        return result;

    if (is_connected())
        return KSuccess;

    if (current->block<Thread::ConnectBlocker>(description) == Thread::BlockResult::InterruptedBySignal)
        return KResult(-EINTR);

#ifdef DEBUG_LOCAL_SOCKET
    kprintf("%s(%u) LocalSocket{%p} connect(%s) status is %s\n", current->process().name().characters(), current->pid(), this, safe_address, to_string(setup_state()));
#endif

    if (!is_connected())
        return KResult(-ECONNREFUSED);
    return KSuccess;
}

KResult LocalSocket::listen(int backlog)
{
    LOCKER(lock());
    if (type() != SOCK_STREAM)
        return KResult(-EOPNOTSUPP);
    set_backlog(backlog);
    kprintf("LocalSocket{%p} listening with backlog=%d\n", this, backlog);
    return KSuccess;
}

void LocalSocket::attach(FileDescription& description)
{
    switch (description.socket_role()) {
    case SocketRole::Accepted:
        ++m_accepted_fds_open;
        break;
    case SocketRole::Connected:
        ++m_connected_fds_open;
        break;
    case SocketRole::Connecting:
        ++m_connecting_fds_open;
        break;
    default:
        break;
    }
}

void LocalSocket::detach(FileDescription& description)
{
    switch (description.socket_role()) {
    case SocketRole::Accepted:
        ASSERT(m_accepted_fds_open);
        --m_accepted_fds_open;
        break;
    case SocketRole::Connected:
        ASSERT(m_connected_fds_open);
        --m_connected_fds_open;
        break;
    case SocketRole::Connecting:
        ASSERT(m_connecting_fds_open);
        --m_connecting_fds_open;
        break;
    default:
        break;
    }
}

bool LocalSocket::can_read(FileDescription& description) const
{
    auto role = description.socket_role();
    if (role == SocketRole::Listener)
        return can_accept();
    if (role == SocketRole::Accepted)
        return !has_attached_peer(description) || !m_for_server.is_empty();
    if (role == SocketRole::Connected)
        return !has_attached_peer(description) || !m_for_client.is_empty();
    ASSERT_NOT_REACHED();
}

bool LocalSocket::has_attached_peer(const FileDescription& description) const
{
    if (description.socket_role() == SocketRole::Accepted)
        return m_connected_fds_open || m_connecting_fds_open;
    if (description.socket_role() == SocketRole::Connected)
        return m_accepted_fds_open;
    ASSERT_NOT_REACHED();
}

bool LocalSocket::can_write(FileDescription& description) const
{
    if (description.socket_role() == SocketRole::Accepted)
        return !has_attached_peer(description) || m_for_client.bytes_in_write_buffer() < 16384;
    if (description.socket_role() == SocketRole::Connected)
        return !has_attached_peer(description) || m_for_server.bytes_in_write_buffer() < 16384;
    ASSERT_NOT_REACHED();
}

ssize_t LocalSocket::sendto(FileDescription& description, const void* data, size_t data_size, int, const sockaddr*, socklen_t)
{
    if (!has_attached_peer(description))
        return -EPIPE;
    if (description.socket_role() == SocketRole::Accepted)
        return m_for_client.write((const u8*)data, data_size);
    if (description.socket_role() == SocketRole::Connected)
        return m_for_server.write((const u8*)data, data_size);
    ASSERT_NOT_REACHED();
}

ssize_t LocalSocket::recvfrom(FileDescription& description, void* buffer, size_t buffer_size, int, sockaddr*, socklen_t*)
{
    auto role = description.socket_role();
    if (role == SocketRole::Accepted) {
        if (!description.is_blocking()) {
            if (m_for_server.is_empty())
                return -EAGAIN;
        }
        return m_for_server.read((u8*)buffer, buffer_size);
    }
    if (role == SocketRole::Connected) {
        if (!description.is_blocking()) {
            if (m_for_client.is_empty())
                return -EAGAIN;
        }
        return m_for_client.read((u8*)buffer, buffer_size);
    }
    ASSERT_NOT_REACHED();
}

StringView LocalSocket::socket_path() const
{
    int len = strnlen(m_address.sun_path, sizeof(m_address.sun_path));
    return { m_address.sun_path, len };
}

String LocalSocket::absolute_path(const FileDescription& description) const
{
    StringBuilder builder;
    builder.append("socket:");
    builder.append(socket_path());

    switch (description.socket_role()) {
    case SocketRole::Listener:
        builder.append(" (listening)");
        break;
    case SocketRole::Accepted:
        builder.appendf(" (accepted from pid %d)", origin_pid());
        break;
    case SocketRole::Connected:
        builder.appendf(" (connected to pid %d)", acceptor_pid());
        break;
    case SocketRole::Connecting:
        builder.append(" (connecting)");
        break;
    default:
        break;
    }

    return builder.to_string();
}
