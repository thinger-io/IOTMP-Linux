#ifndef THINGER_HTTP_MIME_TYPES_HPP
#define THINGER_HTTP_MIME_TYPES_HPP

#include <string>

namespace thinger::http::mime_types {

    static const std::string image_gif                      = "image/gif";
    static const std::string text_html                      = "text/html";
    static const std::string text_plain                     = "text/plain";
    static const std::string image_jpeg                     = "image/jpeg";
    static const std::string image_png                      = "image/png";
    static const std::string text_css                       = "text/css";
    static const std::string text_javascript                = "text/javascript";
    static const std::string application_x_font_woff        = "application/x-font-woff";
    static const std::string image_svg_xml                  = "image/svg+xml";
    static const std::string application_json               = "application/json";
    static const std::string application_msgpack            = "application/msgpack";
    static const std::string application_cbor               = "application/cbor";
    static const std::string application_ubjson             = "application/ubjson";

    static const std::string manifest_json                  = "application/manifest+json";
    static const std::string application_octect_stream      = "application/octet-stream";
    static const std::string application_form_urlencoded    = "application/x-www-form-urlencoded";

    /// Convert a file extension into a MIME type.
    const std::string& extension_to_type(const std::string& extension);

}

#endif
