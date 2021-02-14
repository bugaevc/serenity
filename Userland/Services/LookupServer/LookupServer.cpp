/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "LookupServer.h"
#include "ClientConnection.h"
#include "DNSPacket.h"
#include <AK/ByteBuffer.h>
#include <AK/Debug.h>
#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <LibCore/ConfigFile.h>
#include <LibCore/File.h>
#include <LibCore/LocalServer.h>
#include <LibCore/LocalSocket.h>
#include <LibCore/UDPSocket.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

namespace LookupServer {

static LookupServer* s_the;

LookupServer& LookupServer::the()
{
    ASSERT(s_the);
    return *s_the;
}

LookupServer::LookupServer()
{
    ASSERT(s_the == nullptr);
    s_the = this;

    auto config = Core::ConfigFile::get_for_system("LookupServer");
    dbgln("Using network config file at {}", config->file_name());
    m_nameservers = config->read_entry("DNS", "Nameservers", "1.1.1.1,1.0.0.1").split(',');

    load_etc_hosts();

    m_local_server = Core::LocalServer::construct(this);
    m_local_server->on_ready_to_accept = [this]() {
        auto socket = m_local_server->accept();
        if (!socket) {
            dbgln("Failed to accept a client connection");
            return;
        }
        static int s_next_client_id = 0;
        int client_id = ++s_next_client_id;
        IPC::new_client_connection<ClientConnection>(socket.release_nonnull(), client_id);
    };
    bool ok = m_local_server->take_over_from_system_server();
    ASSERT(ok);
}

void LookupServer::load_etc_hosts()
{
    auto file = Core::File::construct("/etc/hosts");
    if (!file->open(Core::IODevice::ReadOnly))
        return;
    while (!file->eof()) {
        auto line = file->read_line(1024);
        if (line.is_empty())
            break;
        auto fields = line.split('\t');

        auto sections = fields[0].split('.');
        IPv4Address addr {
            (u8)atoi(sections[0].characters()),
            (u8)atoi(sections[1].characters()),
            (u8)atoi(sections[2].characters()),
            (u8)atoi(sections[3].characters()),
        };
        auto raw_addr = addr.to_in_addr_t();

        auto name = fields[1];
        m_etc_hosts.set(name, String { (const char*)&raw_addr, sizeof(raw_addr) });

        IPv4Address reverse_addr {
            (u8)atoi(sections[3].characters()),
            (u8)atoi(sections[2].characters()),
            (u8)atoi(sections[1].characters()),
            (u8)atoi(sections[0].characters()),
        };
        StringBuilder builder;
        builder.append(reverse_addr.to_string());
        builder.append(".in-addr.arpa");
        m_etc_hosts.set(builder.to_string(), name);
    }
}

Vector<String> LookupServer::lookup(const DNSName& name, unsigned short record_type)
{
#if LOOKUPSERVER_DEBUG
    dbgln("Got request for '{}'", name.as_string());
#endif

    Vector<String> responses;

    if (auto known_host = m_etc_hosts.get(name.as_string()); known_host.has_value()) {
        responses.append(known_host.value());
    } else if (!name.as_string().is_empty()) {
        for (auto& nameserver : m_nameservers) {
#if LOOKUPSERVER_DEBUG
            dbgln("Doing lookup using nameserver '{}'", nameserver);
#endif
            bool did_get_response = false;
            int retries = 3;
            do {
                responses = lookup(name, nameserver, did_get_response, record_type);
                if (did_get_response)
                    break;
            } while (--retries);
            if (!responses.is_empty()) {
                break;
            } else {
                if (!did_get_response)
                    dbgln("Never got a response from '{}', trying next nameserver", nameserver);
                else
                    dbgln("Received response from '{}' but no result(s), trying next nameserver", nameserver);
            }
        }
        if (responses.is_empty()) {
            fprintf(stderr, "LookupServer: Tried all nameservers but never got a response :(\n");
            return {};
        }
    }

    return move(responses);
}

Vector<String> LookupServer::lookup(const DNSName& name, const String& nameserver, bool& did_get_response, unsigned short record_type, ShouldRandomizeCase should_randomize_case)
{
    if (auto cached_answers = m_lookup_cache.get(name.as_string()); cached_answers.has_value()) {
        Vector<String> responses;
        for (auto& answer : cached_answers.value()) {
            if (answer.type() == record_type && !answer.has_expired()) {
#if LOOKUPSERVER_DEBUG
                dbgln("Cache hit: {} -> {}", name.as_string(), answer.record_data());
#endif
                responses.append(answer.record_data());
            }
        }
        if (!responses.is_empty())
            return responses;
    }

    DNSPacket request;
    request.set_is_query();
    request.set_id(arc4random_uniform(UINT16_MAX));
    request.add_question(name.as_string(), record_type, should_randomize_case);

    auto buffer = request.to_byte_buffer();

    auto udp_socket = Core::UDPSocket::construct();
    udp_socket->set_blocking(true);

    struct timeval timeout {
        1, 0
    };

    int rc = setsockopt(udp_socket->fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (rc < 0) {
        perror("setsockopt(SOL_SOCKET, SO_RCVTIMEO)");
        return {};
    }

    if (!udp_socket->connect(nameserver, 53))
        return {};

    if (!udp_socket->write(buffer))
        return {};

    u8 response_buffer[4096];
    int nrecv = udp_socket->read(response_buffer, sizeof(response_buffer));
    if (nrecv == 0)
        return {};

    did_get_response = true;

    auto o_response = DNSPacket::from_raw_packet(response_buffer, nrecv);
    if (!o_response.has_value())
        return {};

    auto& response = o_response.value();

    if (response.id() != request.id()) {
        dbgln("LookupServer: ID mismatch ({} vs {}) :(", response.id(), request.id());
        return {};
    }

    if (response.code() == DNSPacket::Code::REFUSED) {
        if (should_randomize_case == ShouldRandomizeCase::Yes) {
            // Retry with 0x20 case randomization turned off.
            return lookup(name, nameserver, did_get_response, record_type, ShouldRandomizeCase::No);
        }
        return {};
    }

    if (response.question_count() != request.question_count()) {
        dbgln("LookupServer: Question count ({} vs {}) :(", response.question_count(), request.question_count());
        return {};
    }

    // Verify the questions in our request and in their response match exactly, including case.
    for (size_t i = 0; i < request.question_count(); ++i) {
        auto& request_question = request.questions()[i];
        auto& response_question = response.questions()[i];
        bool exact_match = request_question.class_code() == response_question.class_code()
            && request_question.record_type() == response_question.record_type()
            && request_question.name().as_string() == response_question.name().as_string();
        if (!exact_match) {
            dbgln("Request and response questions do not match");
            dbgln("   Request: name=_{}_, type={}, class={}", request_question.name().as_string(), response_question.record_type(), response_question.class_code());
            dbgln("  Response: name=_{}_, type={}, class={}", response_question.name().as_string(), response_question.record_type(), response_question.class_code());
            return {};
        }
    }

    if (response.answer_count() < 1) {
        dbgln("LookupServer: Not enough answers ({}) :(", response.answer_count());
        return {};
    }

    Vector<String, 8> responses;
    for (auto& answer : response.answers()) {
        put_in_cache(answer);
        if (answer.type() != record_type)
            continue;
        responses.append(answer.record_data());
    }

    return responses;
}

void LookupServer::put_in_cache(const DNSAnswer& answer)
{
    if (answer.has_expired())
        return;

    // Prevent the cache from growing too big.
    // TODO: Evict least used entries.
    if (m_lookup_cache.size() >= 256)
        m_lookup_cache.remove(m_lookup_cache.begin());

    auto it = m_lookup_cache.find(answer.name().as_string());
    if (it == m_lookup_cache.end())
        m_lookup_cache.set(answer.name().as_string(), { answer });
    else
        it->value.append(answer);
}

}
