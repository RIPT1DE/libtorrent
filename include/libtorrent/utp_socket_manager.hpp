/*

Copyright (c) 2009-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_UTP_SOCKET_MANAGER_HPP_INCLUDED
#define TORRENT_UTP_SOCKET_MANAGER_HPP_INCLUDED

#include <map>

#include "libtorrent/socket_type.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/span.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/function.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	class utp_stream;
	struct utp_socket_impl;
	struct counters;

	struct utp_socket_manager final
	{
		typedef boost::function<void(udp::endpoint const&
			, span<char const>
			, error_code&, int)> send_fun_t;

		typedef boost::function<void(boost::shared_ptr<socket_type> const&)>
			incoming_utp_callback_t;

		utp_socket_manager(send_fun_t const& send_fun
			, incoming_utp_callback_t const& cb
			, io_service& ios
			, aux::session_settings const& sett
			, counters& cnt, void* ssl_context
			);
		~utp_socket_manager();

		// return false if this is not a uTP packet
		bool incoming_packet(udp::endpoint const& ep, span<char const> p);

		// if the UDP socket failed with an EAGAIN or EWOULDBLOCK, this will be
		// called once the socket is writeable again
		void writable();

		// when the upper layer has drained the underlying UDP socket, this is
		// called, and uTP sockets will send their ACKs. This ensures ACKs at
		// least coalese packets returned during the same wakeup
		void socket_drained();

		void tick(time_point now);

		// flags for send_packet
		enum { dont_fragment = 1 };
		void send_packet(udp::endpoint const& ep, char const* p, int len
			, error_code& ec, int flags = 0);
		void subscribe_writable(utp_socket_impl* s);

		// internal, used by utp_stream
		void remove_socket(std::uint16_t id);

		utp_socket_impl* new_utp_socket(utp_stream* str);
		int gain_factor() const { return m_sett.get_int(settings_pack::utp_gain_factor); }
		int target_delay() const { return m_sett.get_int(settings_pack::utp_target_delay) * 1000; }
		int syn_resends() const { return m_sett.get_int(settings_pack::utp_syn_resends); }
		int fin_resends() const { return m_sett.get_int(settings_pack::utp_fin_resends); }
		int num_resends() const { return m_sett.get_int(settings_pack::utp_num_resends); }
		int connect_timeout() const { return m_sett.get_int(settings_pack::utp_connect_timeout); }
		int min_timeout() const { return m_sett.get_int(settings_pack::utp_min_timeout); }
		int loss_multiplier() const { return m_sett.get_int(settings_pack::utp_loss_multiplier); }

		void mtu_for_dest(address const& addr, int& link_mtu, int& utp_mtu);
		int num_sockets() const { return int(m_utp_sockets.size()); }

		void defer_ack(utp_socket_impl* s);
		void subscribe_drained(utp_socket_impl* s);

		void restrict_mtu(int mtu)
		{
			m_restrict_mtu[m_mtu_idx] = mtu;
			m_mtu_idx = (m_mtu_idx + 1) % m_restrict_mtu.size();
		}

		int restrict_mtu() const
		{
			return *std::max_element(m_restrict_mtu.begin(), m_restrict_mtu.end());
		}

		// used to keep stats of uTP events
		// the counter is the enum from ``counters``.
		void inc_stats_counter(int counter, int delta = 1);

	private:
		// explicitly disallow assignment, to silence msvc warning
		utp_socket_manager& operator=(utp_socket_manager const&);

		send_fun_t m_send_fun;
		incoming_utp_callback_t m_cb;

		// replace with a hash-map
		typedef std::multimap<std::uint16_t, utp_socket_impl*> socket_map_t;
		socket_map_t m_utp_sockets;

		// this is a list of sockets that needs to send an ack.
		// once the UDP socket is drained, all of these will
		// have a chance to do that. This is to avoid sending
		// an ack for every single packet
		std::vector<utp_socket_impl*> m_deferred_acks;

		// sockets that have received or sent packets this
		// round, may subscribe to the event of draining the
		// UDP socket. At that point they may call the
		// user callback function to indicate bytes have been
		// sent or received.
		std::vector<utp_socket_impl*> m_drained_event;

		// list of sockets that received EWOULDBLOCK from the
		// underlying socket. They are notified when the socket
		// becomes writable again
		std::vector<utp_socket_impl*> m_stalled_sockets;

		// the last socket we received a packet on
		utp_socket_impl* m_last_socket;

		int m_new_connection;

		aux::session_settings const& m_sett;

		// this is a copy of the routing table, used
		// to initialize MTU sizes of uTP sockets
		mutable std::vector<ip_route> m_routes;

		// the timestamp for the last time we updated
		// the routing table
		mutable time_point m_last_route_update;

		// cache of interfaces
		mutable std::vector<ip_interface> m_interfaces;
		mutable time_point m_last_if_update;

		// the buffer size of the socket. This is used
		// to now lower the buffer size
		int m_sock_buf_size;

		// stats counters
		counters& m_counters;

		io_service& m_ios;

		std::array<int, 3> m_restrict_mtu;
		int m_mtu_idx;

		// this is  passed on to the instantiate connection
		// if this is non-nullptr it will create SSL connections over uTP
		void* m_ssl_context;
	};
}

#endif

