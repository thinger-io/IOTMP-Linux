#include "http_client.hpp"
#include "../asio/sockets/ssl_socket.hpp"
#include "../asio/sockets/unix_socket.hpp"
#include "../asio/workers.hpp"
#include "../util/logger.hpp"

#include <utility>

namespace thinger::http {

    void send_request(const std::shared_ptr<http_request>& request, const std::shared_ptr<http_client_connection>& connection, std::function<void(const boost::system::error_code&, std::shared_ptr<http_response>)> handler, unsigned int hops){
        connection->send_request(request, [request, handler=std::move(handler), hops](const boost::system::error_code& ec, const std::shared_ptr<http::http_response>& response) {
            if(!ec && response){
                // if response status is redirect, handle another query
                if(response->is_redirect_response() && hops<MAX_REQUEST_HOPS) {
                    std::string redirectUrl = response->get_header_with_key(header::location);
                    if (!redirectUrl.empty() && request->set_url(redirectUrl)){
                        if (request->get_cookie_store().update_from_headers(*response)) {
                            request->set_header(http::header::cookie, request->get_cookie_store().get_cookie_string());
                        }
                        send_request(request, handler, hops+1);
                    }else{
                        handler(ec, response);
                    }
                // the status is a definitive status... call original handler
                }else{
                    handler(ec, response);
                }
            }else{
                LOG_ERROR("error while sending request: %s", ec.message().c_str());
                handler(ec, response);
            }
        });
    }

    void send_request(const std::shared_ptr<http_request>& request, const std::function<void(const boost::system::error_code&, std::shared_ptr<http_response>)>& handler, unsigned int hops)
    {
    	send_request(asio::workers.get_thread_io_service(), request, handler, hops);
    }

    std::shared_ptr<http_client_connection> create_http_client_connection(boost::asio::io_service& io_service, const std::shared_ptr<http_request>& request){
        const std::string& socket_path = request->get_unix_socket();

        if(socket_path.empty()){
            std::shared_ptr<thinger::asio::socket> sock;
            if(request->get_protocol()=="iotmp"){
                //sock = std::make_shared<thinger::iotmp_socket>("iotmp_client", io_service);
            }else{
                if(!request->is_ssl()){
                    sock = std::make_shared<thinger::asio::tcp_socket>("http_client", io_service);
                }else{
                    auto ssl_context = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23_client);
                    ssl_context->set_default_verify_paths();
                    sock = std::make_shared<thinger::asio::ssl_socket>("http_client", io_service, ssl_context);
                }
            }

            return std::make_shared<http_client_connection>(sock);
        }else{
            std::shared_ptr<thinger::asio::unix_socket> sock = std::make_shared<thinger::asio::unix_socket>("http_client", io_service);
            return std::make_shared<http_client_connection>(sock, socket_path);
        }
    }

    std::shared_ptr<http_client_connection> get_http_client_connection(boost::asio::io_service& io_service, const std::shared_ptr<http_request>& request, bool cacheable=true){
        static thread_local std::unordered_map<std::string, std::weak_ptr<http_client_connection>> active_connections;

        // avoid sending empty request
        if(!request) return nullptr;

        // not cacheable ? create a new http client connection
        if(!cacheable) return create_http_client_connection(io_service, request);

        // check if we can reuse some cached connection
        std::shared_ptr<http_client_connection> connection;

        const std::string& socket_path = request->get_unix_socket();

        // compute request host
        std::string target_host;
        if(socket_path.empty()){
            target_host = request->get_host() + ":" + request->get_port() + ":" + std::to_string(request->is_ssl());
        }else{
            target_host = "unix://" + socket_path;
        }

        auto connection_it = active_connections.find(target_host);
        if(connection_it != active_connections.end()){
            connection = connection_it->second.lock();
        }

        if(!connection){
            connection = create_http_client_connection(io_service, request);
            active_connections[target_host] = connection;
        }

        return connection;
    }

	void send_request(boost::asio::io_service& io_service,
                      const std::shared_ptr<http_request>& request,
                      const std::function<void(const boost::system::error_code&,
                                               std::shared_ptr<http_response>)>& handler,
                      unsigned int hops)
    {
        send_request(request, get_http_client_connection(io_service, request), handler, hops);
    }

    void send_request(const std::shared_ptr<http_request>& request,
                      std::function<void(const boost::system::error_code&,
                                         std::shared_ptr<http_client_connection>,
                                         std::shared_ptr<http_response>)> handler){
        send_request(asio::workers.get_thread_io_service(), request, std::move(handler));
    }

    void send_request(boost::asio::io_service& io_service,
                      const std::shared_ptr<http_request>& request,
                      std::function<void(const boost::system::error_code&,
                                         std::shared_ptr<http_client_connection>,
                                         std::shared_ptr<http_response>)> handler){

        auto connection = create_http_client_connection(io_service, request);
        send_request(request, connection, [connection, handler=std::move(handler)]
                (const boost::system::error_code& ec, std::shared_ptr<http_response> response){
            handler(ec, connection, std::move(response));
        }, 0);
    }

}
