// Build tool: compresses one file with the Windows compression API (XPRESS Huffman, fast to decompress) for embedding
// in the self-contained debugvision.exe. Usage: dv_compress <input> <output>

#include <windows.h>
#include <compressapi.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 3)
    {
        std::fputs("usage: dv_compress <input> <output>\n", stderr);
        return 1;
    }

    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> input((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof())
    {
        std::fwprintf(stderr, L"dv_compress: cannot read %ls\n", argv[1]);
        return 1;
    }

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor))
    {
        std::fputs("dv_compress: CreateCompressor failed\n", stderr);
        return 1;
    }
    SIZE_T size = 0;
    Compress(compressor, input.data(), input.size(), nullptr, 0, &size);
    std::vector<char> output(size);
    bool ok = Compress(compressor, input.data(), input.size(), output.data(), output.size(), &size);
    CloseCompressor(compressor);
    if (!ok)
    {
        std::fwprintf(stderr, L"dv_compress: cannot compress %ls\n", argv[1]);
        return 1;
    }

    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(output.data(), static_cast<std::streamsize>(size));
    if (!out)
    {
        std::fwprintf(stderr, L"dv_compress: cannot write %ls\n", argv[2]);
        return 1;
    }
    return 0;
}
