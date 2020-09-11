#include "relay_server.hpp"

void relay_server::local_handle_accept( std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error)
{
	local_start_accept();
	if (error) {
		BOOST_LOG_TRIVIAL(debug) <<" handle accept error "<<std::endl;
		return;
	}

	sock_ptr->local_start();
}


void relay_server::init_ssl()
{
	_ctx.load_verify_file("yily.crt");
	_ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	_ctx.use_certificate_file("yily.crt", ssl::context::pem);
	_ctx.use_rsa_private_key_file("key.pem", ssl::context::pem);
}

void relay_server::remote_handle_accept(std::shared_ptr<ssl_relay> sock_ptr, const boost::system::error_code& error)
{
	remote_start_accept();
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "accept ssl connect error: "<<error.message()<<std::endl;
		return;
	}

	sock_ptr->remote_start();
}
void relay_server::remote_start_accept()
{
	auto sock_ptr = std::make_shared<ssl_relay> (_io_context, _ctx, _remote);
	_acceptor.async_accept(sock_ptr->get_ssl_sock().lowest_layer(),
			       std::bind(&relay_server::remote_handle_accept, this, sock_ptr, std::placeholders::_1));
}
