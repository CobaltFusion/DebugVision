# libcurl for the http(s) source (libs/dvcore), as the target CURL::libcurl:
#  - Linux, macOS: the system libcurl when it is installed (e.g. libcurl4-openssl-dev)
#  - Windows, and when there is no system libcurl: a static libcurl built from source with only HTTP(S) and proxy support.
#    TLS is Schannel on Windows (the certificate store of Windows, no OpenSSL DLLs to ship) and OpenSSL elsewhere.

if (NOT WIN32)
  find_package(CURL QUIET)
  if (CURL_FOUND)
    message(STATUS "libcurl: using the system libcurl ${CURL_VERSION_STRING}")
    return()
  endif()
  find_package(OpenSSL QUIET)
  if (NOT OpenSSL_FOUND)
    message(FATAL_ERROR "libcurl or OpenSSL is required for the http(s) source: install libcurl (e.g. libcurl4-openssl-dev) or OpenSSL (e.g. libssl-dev)")
  endif()
endif()
message(STATUS "libcurl: building a static libcurl from source")

include(FetchContent)

block()
  # normal variables, only for curl's own option()s (CMP0077)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS ON)
  set(BUILD_TESTING OFF)

  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)

  if (WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  else()
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
  endif()

  # only HTTP(S); proxies (http, https, socks4, socks5) and proxy authentication stay enabled
  foreach(protocol DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP WEBSOCKETS IPFS)
    set(CURL_DISABLE_${protocol} ON CACHE BOOL "" FORCE)
  endforeach()
  # NTLM is off by default in curl's CMake build, but still common for corporate proxies
  set(CURL_ENABLE_NTLM ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_AWS ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_DOH ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)

  # no optional libraries: nothing to find or to ship
  foreach(dependency CURL_ZLIB CURL_BROTLI CURL_ZSTD)
    set(${dependency} OFF CACHE STRING "" FORCE)
  endforeach()
  foreach(dependency USE_NGHTTP2 USE_NGHTTP3 USE_LIBIDN2 USE_WIN32_IDN CURL_USE_LIBPSL CURL_USE_LIBSSH2 CURL_USE_LIBSSH CURL_USE_GSSAPI CURL_USE_GSASL USE_LIBRTMP)
    set(${dependency} OFF CACHE BOOL "" FORCE)
  endforeach()

  FetchContent_Declare(curl
    URL https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
    URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(curl)
endblock()
