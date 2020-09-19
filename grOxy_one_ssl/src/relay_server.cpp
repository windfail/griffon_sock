#include "relay_server.hpp"

void relay_server::local_server_start( )
{
	auto server = std::make_shared<ssl_relay> (_io_context, _ctx, _remote, _lport);
	server->local_start_accept();

}


void relay_server::init_ssl()
{
	_ctx.load_verify_file("yily.crt");
	_ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	_ctx.use_certificate_file("yily.crt", ssl::context::pem);
	_ctx.use_rsa_private_key_file("key.pem", ssl::context::pem);
}

void relay_server::remote_handle_accept(std::shared_ptr<ssl_relay> ssl_ptr, const boost::system::error_code& error)
{
	remote_server_start();
	if (error) {
		BOOST_LOG_TRIVIAL(info) << "accept ssl connect error: "<<error.message();
		return;
	}

	ssl_ptr->remote_ssl_start();
}
void relay_server::remote_server_start()
{
	auto ssl_ptr = std::make_shared<ssl_relay> (_io_context, _ctx);
	_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(),
			       std::bind(&relay_server::remote_handle_accept, this, ssl_ptr, std::placeholders::_1));
}
