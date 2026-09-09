#include "NetworkRoutes.h"

namespace gte::Network {

std::string HandleHelloWorld()
{
    return "hello world";
}

CaptureResponseFormat ResolveCaptureResponseFormat(const std::string& queryFormat, const std::string& acceptHeader)
{
    if (queryFormat == "png") {
        return CaptureResponseFormat::RawPng;
    }
    if (queryFormat == "base64" || queryFormat == "json") {
        return CaptureResponseFormat::JsonBase64;
    }
    if (!queryFormat.empty()) {
        // Unrecognized ?format= value - not an error, falls back to the
        // default (see this function's own doc comment in NetworkRoutes.h).
        return CaptureResponseFormat::RawPng;
    }
    if (acceptHeader.find("application/json") != std::string::npos) {
        return CaptureResponseFormat::JsonBase64;
    }
    return CaptureResponseFormat::RawPng;
}

std::string BuildCaptureJsonBody(int width, int height, const std::string& base64Png)
{
    std::string body;
    body.reserve(base64Png.size() + 64);
    body += "{\"width\":";
    body += std::to_string(width);
    body += ",\"height\":";
    body += std::to_string(height);
    body += ",\"format\":\"png\",\"data_base64\":\"";
    body += base64Png;
    body += "\"}";
    return body;
}

} // namespace gte::Network
