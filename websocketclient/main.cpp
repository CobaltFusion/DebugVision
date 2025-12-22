#include "httplib.h"

#include <format>
#include <print>

int main()
{
    httplib::Server svr;

    svr.set_mount_point("/", "C:/project/debugvision/websocketclient");

    // svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
    //     res.set_content("Hello World!", "text/plain");
    // });

    svr.set_error_handler([](const auto&  req , auto& res) {
        auto fmt = std::format("<p>Error Status: <span style='color:red;'>{}</span> <span style='color:black;'>Path: '{}'</span></p>", res.status, req.path);
        res.set_content(fmt.c_str(), "text/html");
    });

    std::print("listening...\n");
    svr.listen("0.0.0.0", 8080);
    std::print("done...\n");

    return 0;
}

