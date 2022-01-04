#include <format>

int main()
{
    auto msg = std::format("Hello world!\n");
    puts(msg.data());
}
